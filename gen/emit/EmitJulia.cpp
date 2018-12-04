#include <algorithm>
#include <iostream>
#include <sstream>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Common.hpp"
#include "Emitter.hpp"
#include "GetOpt.hpp"
#include "ZCMGen.hpp"

#include "util/StringUtil.hpp"
#include "util/FileUtil.hpp"

using namespace std;

void setupOptionsJulia(GetOpt& gopt)
{
    gopt.addString(0, "julia-path", ".",
                      "Julia destination directory");

    gopt.addString(0, "julia-pkg-prefix", "",
                      "Julia package prefix, all types/packages will be inside this. "
                      "Comes *before* global pkg-prefix if both specified.");

    // Note: The reason we DON'T generate the actual zcmtype files during this call is that
    //       the types themselves may have differing options (such as endian-ness).
    gopt.addBool(  0, "julia-generate-pkg-files", false,
                      "Generates the pkg file(s) instead of the zcmtype files. "
                      "MUST HAVE ALL ZCMTYPES FROM PACKAGE(S) INCLUDED IN COMMAND");

    gopt.addBool(  0, "julia-disable-runtime-assertions", false,
                      "Disable runtime assertions (in encode) for type/size checking in Julia");
}

// Converts zcm type names into their julia equivalent, accounting for packaging for nonprimatives
static string mapTypeName(const string& t, const string& pkgPrefix = "")
{
    if      (t == "int8_t")   return "Int8";
    else if (t == "int16_t")  return "Int16";
    else if (t == "int32_t")  return "Int32";
    else if (t == "int64_t")  return "Int64";
    else if (t == "byte")     return "UInt8";
    else if (t == "float")    return "Float32";
    else if (t == "double")   return "Float64";
    else if (t == "string")   return "String";
    else if (t == "boolean")  return "Bool";
    else if (pkgPrefix.empty() && StringUtil::split(t, '.').size() == 1) {
        return "__basemodule._" + t + "." + t;
    } else {
        return "__basemodule." + pkgPrefix + t;
    }
}

static string topLevelPackage(const string& package)
{
    auto packageParts = StringUtil::split(package, '.');
    if (packageParts.size() > 0) {
        return packageParts[0];
    } else {
        return "";
    }
}

struct EmitJuliaType : public Emitter
{
    const ZCMGen& zcm;
    const ZCMStruct& zs;
    string pkg;

    string hton, ntoh;
    string pkgPrefix;

    bool enableRuntimeAssertions;

    EmitJuliaType(const ZCMGen& zcm, const string& pkg, const ZCMStruct& zs):
        Emitter(getFilename(zcm, pkg, zs.structname.shortname, true)),
        zcm(zcm), zs(zs), pkg(pkg),
        hton(zcm.gopt->getBool("little-endian-encoding") ? "htol" : "hton"),
        ntoh(zcm.gopt->getBool("little-endian-encoding") ? "ltoh" : "ntoh"),
        pkgPrefix(zcm.gopt->getString("julia-pkg-prefix")),
        enableRuntimeAssertions(!zcm.gopt->getBool("julia-disable-runtime-assertions"))
    {
        if (!pkgPrefix.empty()) pkgPrefix += ".";
    }

    static string getFilename(const ZCMGen& zcm, const string& pkg, const string& type,
                              bool ensureDirectoryExists = false)
    {
        assert(!type.empty());

        // create the package directory, if necessary
        string pathPrefix = zcm.gopt->getString("julia-path");
        vector<string> pkgs = StringUtil::split(pkg, '.');
        string filename = "_" + type + ".jl";

        string pkgDirs = StringUtil::join(pkgs, '/');;
        string pkgPath = (pathPrefix.empty() ? pathPrefix : pathPrefix + "/") +
                         (   pkgDirs.empty() ?    pkgDirs :    pkgDirs + "/");

        if (ensureDirectoryExists && pkgPath != "") {
            if (!FileUtil::exists(pkgPath)) {
                FileUtil::mkdirWithParents(pkgPath, 0755);
            }
            if (!FileUtil::dirExists(pkgPath)) {
                cerr << "Could not create directory " << pkgPath << "\n";
                return "";
            }
        }

        return pkgPath + filename;
    }

    static vector<string> getDependencies(const ZCMGen& zcm, const string& pkg, const ZCMStruct& zs)
    {
        set<string> deps {};

        string pkgPrefix = zcm.gopt->getString("julia-pkg-prefix");
        auto topLvlPkg = topLevelPackage(pkg);
        for (auto& zm : zs.members) {
            if (zcm.isPrimitiveType(zm.type.fullname)) continue;
            if (zm.type.fullname == zs.structname.fullname) continue;

            string joiner = (pkgPrefix.empty() || zm.type.package.empty()) ? "" : ".";
            auto memberPkg = pkgPrefix + joiner + zm.type.package;
            if (memberPkg.empty()) {
                deps.insert("_" + zm.type.shortname + ": " + zm.type.shortname);
            } else {
                auto memberTopLvlPkg = topLevelPackage(memberPkg);
                if (topLvlPkg != memberTopLvlPkg) {
                    deps.insert(memberTopLvlPkg);
                }
            }
        }

        return vector<string> {deps.begin(), deps.end()};
    }

    void emitAutoGeneratedWarning()
    {
        emit(0, "# THIS IS AN AUTOMATICALLY GENERATED FILE.");
        emit(0, "# DO NOT MODIFY BY HAND!!");
        emit(0, "#");
        emit(0, "# Generated by zcm-gen");
        emit(0, "#");
        emit(0, "");
    }

    void emitComment(int indent, const string& comment)
    {
        if (comment == "")
            return;

        auto lines = StringUtil::split(comment, '\n');
        if (lines.size() == 1) {
            emit(indent, "# %s", lines[0].c_str());
        } else {
            for (auto& line : lines) {
                emitStart(indent, "#");
                if (line.size() > 0)
                    emitContinue("%s", line.c_str());
                emitEnd("");
            }
        }
    }

    void emitPreDependencies()
    {
        emit(0, "import ZCM");
    }

    void emitModuleInit()
    {
        if (!pkg.empty()) return;

        emit(0, "function __init__()");

        auto deps = getDependencies(zcm, pkg, zs);
        for (auto& dep : deps) {
            emit(1, "eval(__basemodule, parse(\"import %s\"))", dep.c_str());
        }

        emit(0, "end");
    }

    void emitModuleStart()
    {
        emitAutoGeneratedWarning();
        if (zs.structname.package.empty()) {
            emit(0, "# This file intended to be imported by user");
            emit(0, "# after setting up their LOAD_PATH,");
            emit(0, "# but you must import the type directly into the user's module:");
            emit(0, "#     unshift!(LOAD_PATH, \"path/to/dir/containing/this/file\")");
            emit(0, "#     import _%s : %s", zs.structname.shortname.c_str(),
                                             zs.structname.shortname.c_str());
            emit(0, "module _%s", zs.structname.shortname.c_str());
            emit(0, "__basemodule = module_parent(_%s)", zs.structname.shortname.c_str());
        } else {
            emit(0, "begin");
            emitStart(0, "@assert (endswith(string(current_module()), \"%s\"))",
                         zs.structname.package.c_str());
            emitEnd(" \"Only import this file through its module\"");
        }
        emit(0, "");
        emitPreDependencies();
        emit(0, "");
        emitModuleInit();
        emit(0, "");
    }

    void emitModuleEnd()
    {
        if (zs.structname.package.empty()) {
            emit(0, "end # `module _%s` block", zs.structname.nameUnderscoreCStr());
        } else {
            emit(0, "end # `begin` block");
        }
    }

    void emitInstance()
    {
        const char* sn = zs.structname.shortname.c_str();

        // define the class
        emitComment(0, zs.comment);
        emit(0, "export %s", sn);
        emit(0, "type %s <: ZCM.AbstractZcmType", sn);
        emit(0, "");

        // data members
        if (zs.members.size() > 0) {
            emit(1, "# **********************");
            emit(1, "# Members");
            emit(1, "# **********************\n");
            for (auto& zm : zs.members) {
                auto& mtn = zm.type.fullname;
                emitComment(1, zm.comment);

                string mappedTypename;
                if (zcm.isPrimitiveType(mtn)) {
                    mappedTypename = mapTypeName(mtn);
                } else {
                    mappedTypename = "ZCM.AbstractZcmType";
                }

                int ndim = (int)zm.dimensions.size();
                if (ndim == 0) {
                    emitStart(1, "%-30s::%s", zm.membername.c_str(), mappedTypename.c_str());
                } else {
                    emitStart(1, "%-30s::Array{%s,%u}", zm.membername.c_str(),
                                                        mappedTypename.c_str(), ndim);
                }

                if (!zcm.isPrimitiveType(mtn)) {
                    emitContinue(" # %s", (pkgPrefix + mtn).c_str());
                }

                emitEnd("");
            }
            emit(0, "");
        }

        // TODO: currently, you need an instance of the type to access the constants
        // constants
        if (zs.constants.size() > 0) {
            emit(0, "");
            emit(1, "# **********************");
            emit(1, "# Constants");
            emit(1, "# **********************\n");
            for (auto& zc : zs.constants) {
                assert(ZCMGen::isLegalConstType(zc.type));
                string mt = mapTypeName(zc.type);
                emit(1, "%-30s::%s", zc.membername.c_str(), mt.c_str(), zc.valstr.c_str());
            }
            emit(0, "");
        }

        emit(0, "");
        emit(1, "function %s()", sn);
        emit(0, "");
        emit(2, "self = new()");
        emit(0, "");

        // data members
        if (zs.members.size() > 0) {
            emit(2, "# **********************");
            emit(2, "# Members");
            emit(2, "# **********************\n");
            for (size_t i = 0; i < zs.members.size(); ++i) {
                auto& zm = zs.members[i];
                emitStart(2, "self.%s = ", zm.membername.c_str());
                emitMemberInitializer(zm);
                emitEnd("");
            }
            emit(0, "");
        }

        // constants
        if (zs.constants.size() > 0) {
            emit(2, "# **********************");
            emit(2, "# Constants");
            emit(2, "# **********************\n");

            for (auto& zc : zs.constants) {
                assert(ZCMGen::isLegalConstType(zc.type));
                string mt = mapTypeName(zc.type);
                emitStart(2, "self.%s::%s = ", zc.membername.c_str(), mt.c_str());
                if (zc.isFixedPoint() && zc.valstr.substr(0, 2) == "0x")
                    emitEnd("reinterpret(%s,%s)", mt.c_str(), zc.valstr.c_str());
                else
                    emitEnd("%s", zc.valstr.c_str());
            }
            emit(0, "");
        }

        emit(2, "return self");
        emit(1, "end");
        emit(0, "");
        emit(0, "end");
        emit(0, "");
    }

    void emitMemberInitializer(const ZCMMember& zm)
    {
        auto& tn = zm.type.fullname;
        string initializer;
        if      (tn == "byte")    initializer = "0";
        else if (tn == "boolean") initializer = "false";
        else if (tn == "int8_t")  initializer = "0";
        else if (tn == "int16_t") initializer = "0";
        else if (tn == "int32_t") initializer = "0";
        else if (tn == "int64_t") initializer = "0";
        else if (tn == "float")   initializer = "0.0";
        else if (tn == "double")  initializer = "0.0";
        else if (tn == "string")  initializer = "\"\"";
        else if (pkgPrefix.empty() && zm.type.package.empty()) {
            initializer = "__basemodule._" + tn + "." + tn + "()";
        } else {
            initializer = "__basemodule." + pkgPrefix + tn + "()";
        }

        if (zm.dimensions.size() == 0) {
            emitContinue("%s", initializer.c_str());
        } else {
            emitContinue("[ %s for", initializer.c_str());
            for (size_t i = 0; i < zm.dimensions.size(); ++i) {
                auto& dim = zm.dimensions[i];
                if (i == 0) emitContinue(" ");
                else        emitContinue(", ");

                emitContinue("dim%d=1:", i);
                if (dim.mode == ZCM_CONST) emitContinue("%s",      dim.size.c_str());
                else                       emitContinue("self.%s", dim.size.c_str());
            }
            emitContinue(" ]");
        }
    }

    void emitGetHash()
    {
        auto* sn = zs.structname.shortname.c_str();
        auto* fn = zs.structname.nameUnderscoreCStr();

        emit(0, "const __%s_hash = Ref(Int64(0))", fn);
        emit(0, "function ZCM._get_hash_recursive(::Type{%s}, parents::Array{String})", sn);
        emit(1,     "if __%s_hash[] != 0; return __%s_hash[]; end", fn, fn);
        emit(1,     "if \"%s\" in parents; return 0; end", fn);
        for (auto& zm : zs.members) {
            if (!ZCMGen::isPrimitiveType(zm.type.fullname)) {
                emit(1, "newparents::Array{String} = [parents[:]; \"%s\"::String];", fn);
                break;
            }
        }
        emitStart(1, "hash::UInt64 = 0x%" PRIx64, zs.hash);
        for (auto &zm : zs.members) {
            if (!ZCMGen::isPrimitiveType(zm.type.fullname)) {
                string mtn = "__basemodule." + pkgPrefix + zm.type.fullname;
                if (pkgPrefix.empty() && zm.type.package.empty())
                    mtn = "__basemodule._" + zm.type.fullname + "." + zm.type.fullname;
                emitContinue(" + reinterpret(UInt64, ZCM._get_hash_recursive(%s, newparents))",
                             mtn.c_str());
            }
        }
        emitEnd("");

        emit(1,     "hash = (hash << 1) + ((hash >>> 63) & 0x01)");
        emit(1,     "__%s_hash[] = reinterpret(Int64, hash)", fn);
        emit(1,     "return __%s_hash[]", fn);
        emit(0, "end");
        emit(0, "");
        emit(0, "function ZCM.getHash(::Type{%s})", sn);
        emit(1,     "return ZCM._get_hash_recursive(%s, Array{String,1}())", sn);
        emit(0, "end");
        emit(0, "");
    }

    void emitEncodeSingleMember(const ZCMMember& zm, const string& accessor_, int indent)
    {
        const string& tn = zm.type.fullname;
        auto* accessor = accessor_.c_str();

        if (tn == "string") {
            emit(indent, "write(buf, %s(UInt32(length(%s) + 1)))", hton.c_str(), accessor);
            emit(indent, "write(buf, %s)", accessor);
            emit(indent, "write(buf, UInt8(0))");
        } else if (tn == "boolean") {
            emit(indent, "write(buf, %s)", accessor);
        } else if (tn == "byte"    || tn == "int8_t"  ||
                   tn == "int16_t" || tn == "int32_t" || tn == "int64_t" ||
                   tn == "float"   || tn == "double") {
            emit(indent, "write(buf, %s(%s))", hton.c_str(), accessor);
        } else {
            emit(indent, "ZCM._encode_one(%s,buf)", accessor);
        }
    }

    void emitEncodeListMember(const ZCMMember& zm, const string& accessor_, int indent,
                              const string& len_, int fixedLen)
    {
        auto& tn = zm.type.fullname;
        auto* accessor = accessor_.c_str();
        auto* len = len_.c_str();

        if (tn == "byte" || tn == "boolean" || tn == "int8_t" ||
            tn == "int16_t" || tn == "int32_t" || tn == "int64_t" ||
            tn == "float"  || tn == "double") {
            if (tn != "boolean")
                // XXX: seems like this is actually changing the value in the msg?
                emit(indent, "for i=1:%s%s %s[i] = %s(%s[i]) end",
                             (fixedLen ? "" : "msg."), len, accessor, hton.c_str(), accessor);
            emit(indent, "write(buf, %s[1:%s%s])",
                 accessor, (fixedLen ? "" : "msg."), len);
            return;
        } else {
            assert(0);
        }
    }

    void emitEncodeOne()
    {
        auto* sn = zs.structname.shortname.c_str();
        auto* fn = zs.structname.fullname.c_str();

        emit(0, "function ZCM._encode_one(msg::%s, buf)", sn);
        if (zs.members.size() == 0) {
            emit(1, "return nothing");
            emit(0, "end");
            return;
        }

        for (auto& zm : zs.members) {
            auto& mtn = zm.type.fullname;
            string mappedTypename = mapTypeName(mtn, pkgPrefix);

            if (zm.dimensions.size() == 0) {
                if (enableRuntimeAssertions && !zcm.isPrimitiveType(mtn))
                    emit(1, "@assert isa(msg.%s, %s) "
                            "\"Msg of type `%s` requires field `%s` to be of type `%s`\"",
                            zm.membername.c_str(), mappedTypename.c_str(),
                            fn, zm.membername.c_str(), mtn.c_str());

                emitEncodeSingleMember(zm, "msg." + zm.membername, 1);
            } else {
                string accessor = "msg." + zm.membername;

                size_t n;
                if (enableRuntimeAssertions) {
                    for (n = 0; n < zm.dimensions.size(); ++n) {
                        auto& dim = zm.dimensions[n];

                        string sz;
                        if (dim.mode == ZCM_CONST) sz = dim.size;
                        else                       sz = "msg." + dim.size;

                        emit(1, "@assert size(msg.%s,%d)==%s "
                                "\"Msg of type `%s` requires field `%s` dimension `%d` "
                                "to be size `%s`\"",
                                zm.membername.c_str(), n + 1, sz.c_str(),
                                fn, zm.membername.c_str(), n + 1,
                                sz.c_str());
                    }
                }

                accessor += "[";
                for (n = 0; n < zm.dimensions.size(); ++n) {
                    auto& dim = zm.dimensions[n];

                    if (dim.mode == ZCM_CONST) emit(n + 1, "for i%d=1:%s",     n, dim.size.c_str());
                    else                       emit(n + 1, "for i%d=1:msg.%s", n, dim.size.c_str());

                    if (n > 0) accessor += ",";
                    accessor += "i" + to_string(n);
                }
                accessor += "]";

                if (enableRuntimeAssertions && !zcm.isPrimitiveType(mtn)) {
                    emit(n + 1, "@assert isa(%s, %s) "
                                "\"Msg of type `%s` requires field `%s` to be of type `%s`\"",
                                accessor.c_str(), mappedTypename.c_str(),
                                fn, accessor.c_str(), mtn.c_str());
                }

                emitEncodeSingleMember(zm, accessor, n + 1);

                for (n = 0; n < zm.dimensions.size(); ++n)
                    emit(zm.dimensions.size() - n, "end");

                /* TODO: probably can make use of encoding more than 1 element at once for prims
                // last dimension.
                auto& lastDim = zm.dimensions[zm.dimensions.size() - 1];
                bool lastDimFixedLen = (lastDim.mode == ZCM_CONST);

                if (ZCMGen::isPrimitiveType(zm.type.fullname) &&
                    zm.type.fullname != "string") {
                    emitEncodeListMember(zm, accessor, n + 1, lastDim.size, lastDimFixedLen);
                }
                */
            }
        }

        emit(0, "end");
        emit(0, "");
    }

    void emitEncode()
    {
        auto* sn = zs.structname.shortname.c_str();

        emit(0, "function ZCM.encode(msg::%s)", sn);
        emit(1,     "buf = IOBuffer()");
        emit(1,     "write(buf, %s(ZCM.getHash(%s)))", hton.c_str(), sn);
        emit(1,     "ZCM._encode_one(msg, buf)");
        emit(1,     "return ZCM._takebuf_array(buf);");
        emit(0, "end");
        emit(0, "");
    }

    void emitDecodeSingleMember(const ZCMMember& zm, const string& accessor_,
                                int indent, const string& sfx_)
    {
        auto& tn = zm.type.fullname;
        string mappedTypename = mapTypeName(tn, pkgPrefix);

        auto* accessor = accessor_.c_str();
        auto* sfx = sfx_.c_str();

        if (tn == "string") {
            emit(indent, "%sString(read(buf, %s(reinterpret(UInt32, read(buf, 4))[1])))[1:end-1]%s",
                         accessor, ntoh.c_str(), sfx);
        } else if (tn == "byte"    || tn == "boolean" || tn == "int8_t") {
            auto typeSize = ZCMGen::getPrimitiveTypeSize(tn);
            emit(indent, "%sreinterpret(%s, read(buf, %u))[1]%s",
                         accessor, mappedTypename.c_str(), typeSize, sfx);
        } else if (tn == "int16_t" || tn == "int32_t" || tn == "int64_t" ||
                   tn == "float"   || tn == "double") {
            auto typeSize = ZCMGen::getPrimitiveTypeSize(tn);
            emit(indent, "%s%s(reinterpret(%s, read(buf, %u))[1])%s",
                         accessor, ntoh.c_str(), mappedTypename.c_str(), typeSize, sfx);
        } else {
            emit(indent, "%sZCM._decode_one(%s,buf)%s", accessor, mappedTypename.c_str(), sfx);
        }
    }

    void emitDecodeListMember(const ZCMMember& zm, const string& accessor_, int indent,
                              bool isFirst, const string& len_, bool fixedLen)
    {
        auto& tn = zm.type.fullname;
        string mappedTypename = mapTypeName(tn, pkgPrefix);
        const char* suffix = isFirst ? "" : ")";
        auto* accessor = accessor_.c_str();
        auto* len = len_.c_str();

        if (tn == "byte" || tn == "boolean" || tn == "int8_t" ) {
            if (fixedLen) {
                emit(indent, "%sreinterpret(%s, read(buf, %d))%s",
                     accessor, mappedTypename.c_str(),
                     atoi(len) * ZCMGen::getPrimitiveTypeSize(tn),
                     suffix);
            } else {
                emit(indent, "%sreinterpret(%s, read(buf, (msg.%s) * %lu))%s",
                     accessor, mappedTypename.c_str(),
                     len, ZCMGen::getPrimitiveTypeSize(tn),
                     suffix);
            }
        } else if (tn == "int16_t" || tn == "int32_t" || tn == "int64_t" ||
                   tn == "float"   || tn == "double") {
            if (fixedLen) {
                emit(indent, "%s%s.(reinterpret(%s, read(buf, %d)))%s",
                     accessor, ntoh.c_str(), mappedTypename.c_str(),
                     atoi(len) * ZCMGen::getPrimitiveTypeSize(tn),
                     suffix);
            } else {
                emit(indent, "%s%s.(reinterpret(%s, read(buf, (msg.%s) * %lu)))%s",
                     accessor, ntoh.c_str(), mappedTypename.c_str(),
                     len, ZCMGen::getPrimitiveTypeSize(tn),
                     suffix);
            }
        } else {
            assert(0);
        }
    }

    void emitDecodeOne()
    {
        auto* sn = zs.structname.shortname.c_str();

        emit(0, "function ZCM._decode_one(::Type{%s}, buf)", sn);
        emit(1,     "msg = %s();", sn);

        for (auto& zm : zs.members) {
            if (zm.dimensions.size() == 0) {
                string accessor = "msg." + zm.membername + " = ";
                emitDecodeSingleMember(zm, accessor.c_str(), 1, "");
            } else {
                string accessor = "msg." + zm.membername;
                size_t n = 0;

                auto& mtn = zm.type.fullname;
                string mappedTypename;
                if (zcm.isPrimitiveType(mtn)) mappedTypename = mapTypeName(mtn);
                else                          mappedTypename = "ZCM.AbstractZcmType";

                // emit array initializer for sizing
                emitStart(1, "%s = Array{%s, %d}(",
                             accessor.c_str(), mappedTypename.c_str(), zm.dimensions.size());
                for (n = 0; n < zm.dimensions.size(); ++n) {
                    auto& dim = zm.dimensions[n];

                    if (n > 0) emitContinue(",");
                    if (dim.mode == ZCM_CONST) emitContinue("%s",     dim.size.c_str());
                    else                       emitContinue("msg.%s", dim.size.c_str());
                }
                emitEnd(")");

                // iterate through the dimensions of the member, building up
                // an accessor string, and emitting for loops
                accessor += "[";
                for (n = 0; n < zm.dimensions.size(); ++n) {
                    auto& dim = zm.dimensions[n];

                    if (dim.mode == ZCM_CONST) emit(n + 1, "for i%d=1:%s",     n, dim.size.c_str());
                    else                       emit(n + 1, "for i%d=1:msg.%s", n, dim.size.c_str());

                    if (n > 0) accessor += ",";
                    accessor += "i" + to_string(n);
                }
                accessor += "] = ";

                emitDecodeSingleMember(zm, accessor, n + 1, "");

                for (n = 0; n < zm.dimensions.size(); ++n)
                    emit(zm.dimensions.size() - n, "end");


                /* TODO: probably can make use of decoding more than 1 element at once for prims
                // last dimension.
                auto& lastDim = zm.dimensions[zm.dimensions.size()-1];
                bool lastDimFixedLen = (lastDim.mode == ZCM_CONST);

                if (ZCMGen::isPrimitiveType(zm.type.fullname) &&
                    zm.type.fullname != "string") {
                    // member is a primitive non-string type.  Emit code to
                    // decode a full array in one call to struct.unpack
                    if(n == 0) {
                        accessor += " = ";
                    } else {
                        accessor += ".append(";
                    }

                    emitDecodeListMember(zm, accessor, n + 1, n==0,
                                         lastDim.size, lastDimFixedLen);
                }
                */
            }
        }
        emit(1, "return msg");
        emit(0, "end");
        emit(0, "");
    }

    void emitDecode()
    {
        auto* sn = zs.structname.shortname.c_str();

        emit(0, "function ZCM.decode(::Type{%s}, data::Vector{UInt8})", sn);
        emit(1,     "buf = IOBuffer(data)");
        emit(1,     "if %s(reinterpret(Int64, read(buf, 8))[1]) != ZCM.getHash(%s)",
                ntoh.c_str(), sn);
        emit(2,         "throw(\"Decode error\")");
        emit(1,     "end");
        emit(1,     "return ZCM._decode_one(%s, buf)", sn);
        emit(0, "end");
        emit(0, "");
    }

    int emitType()
    {
        emitModuleStart();
        emitInstance();
        emitGetHash();
        emitEncodeOne();
        emitEncode();
        emitDecodeOne();
        emitDecode();
        emitModuleEnd();
        return 0;
    }
};

struct EmitJuliaPackage : public Emitter
{
    const ZCMGen& zcm;
    string pkg;
    vector<string> pkgs;

    EmitJuliaPackage(const ZCMGen& zcm, const string& pkg) :
        Emitter(getFilename(zcm, pkg, true)),
        zcm(zcm)
    {
        pkgs = StringUtil::split(pkg, '.');
        this->pkg = pkgs.empty() ? "" : pkgs.back();
    }

    static string getFilename(const ZCMGen& zcm, const string& pkg,
                              bool ensureDirectoryExists = false)
    {
        assert(!pkg.empty());

        // create the package directory, if necessary
        string pathPrefix = zcm.gopt->getString("julia-path");
        vector<string> pkgs = StringUtil::split(pkg, '.');
        string filename = pkgs.back() + ".jl";
        pkgs.pop_back();

        string pkgDirs = StringUtil::join(pkgs, '/');;
        string pkgPath = (pathPrefix.empty() ? pathPrefix : pathPrefix + "/") +
                         (   pkgDirs.empty() ?    pkgDirs :    pkgDirs + "/");

        if (ensureDirectoryExists && pkgPath != "") {
            if (!FileUtil::exists(pkgPath)) {
                FileUtil::mkdirWithParents(pkgPath, 0755);
            }
            if (!FileUtil::dirExists(pkgPath)) {
                cerr << "Could not create directory " << pkgPath << "\n";
                return "";
            }
        }

        return pkgPath + filename;
    }

    void emitModuleStart(const vector<const ZCMStruct*>& pkgStructs)
    {
        emit(0, "\"\"\"");
        emit(0, "THIS IS AN AUTOMATICALLY GENERATED FILE.");
        emit(0, "DO NOT MODIFY BY HAND!!");
        emit(0, "Generated by zcm-gen\n");

        if (pkgs.size() == 1) {
            emit(0, "This module intended to be imported by the user");
            emit(0, "after setting up their LOAD_PATH:");
            emit(0, "    unshift!(LOAD_PATH, \"path/to/dir/containing/this/file\")");
            emit(0, "    import %s", pkg.c_str());
        } else {
            emit(0, "This module should only be imported by it's parent, %s",
                    pkgs[pkgs.size() - 2].c_str());
        }

        emit(0, "\n\"\"\"");
        emit(0, "module %s", pkg.c_str());
        if (pkgs.size() != 1) {
            emitStart(0, "@assert (endswith(string(current_module()), \"%s\"))",
                         StringUtil::join(pkgs, '.').c_str());
            emitEnd(" \"Only import this module through its parent\"");
        }
        emit(0, "");

        emitStart(0, "__basemodule = ");
        for (size_t i = 0; i < pkgs.size(); ++i) {
            emitContinue("module_parent(");
        }
        emitContinue("%s", pkg.c_str());
        for (size_t i = 0; i < pkgs.size(); ++i) {
            emitContinue(")");
        }
        emitEnd("");
        emit(0, "__modulepath = joinpath(dirname(@__FILE__), \"%s\")", pkg.c_str());
        emit(0, "unshift!(LOAD_PATH, __modulepath)");
        emit(0, "");
        emit(0, "function __init__()");

        set<string> allDeps {};
        for (auto* zs : pkgStructs) {
            auto deps = EmitJuliaType::getDependencies(zcm, StringUtil::join(pkgs, "."), *zs);
            allDeps.insert(deps.begin(), deps.end());
        }
        for (auto& dep : allDeps) {
            emit(1, "eval(__basemodule, parse(\"import %s\"))", dep.c_str());
        }

        emit(0, "end");
        emit(0, "");
        emit(0, "try");
    }

    void emitModuleEnd()
    {
        emit(0, "finally");
        emit(1, "shift!(LOAD_PATH)");
        emit(0, "end");
        emit(0, "");
        emit(0, "end # module %s;", pkg.c_str());
        if (pkgs.size() != 1) {
            emit(0, "export %s", pkg.c_str());
        }
    }

    void emitSubmodules(const vector<string>& pkgSubmods)
    {
        emit(1, "# Submodules");
        for (auto& s : pkgSubmods) {
            emit(1, "include(joinpath(__modulepath, \"%s.jl\"))", s.c_str());
        }
        emit(0, "");
    }

    void emitTypes(const vector<const ZCMStruct*>& pkgStructs)
    {
        emit(1, "# Types");
        for (auto& s : pkgStructs) {
            emit(1, "include(joinpath(__modulepath, \"_%s.jl\"))", s->structname.shortname.c_str());
        }
        emit(0, "");
    }

    int emitPackage(const vector<string>& pkgSubmods, const vector<const ZCMStruct*>& pkgStructs)
    {
        emitModuleStart(pkgStructs);
        emitSubmodules(pkgSubmods);
        emitTypes(pkgStructs);
        emitModuleEnd();
        return 0;
    }
};

int emitJulia(const ZCMGen& zcm)
{
    string pkgPrefix = zcm.gopt->getString("julia-pkg-prefix");

    bool genPkgFiles = zcm.gopt->getBool("julia-generate-pkg-files");

    // Map of packages to their submodules and structs
    unordered_map<string, std::pair<vector<string>, vector<const ZCMStruct*>>> packages;
    // Add all stucts
    for (auto& zs : zcm.structs) {
        auto package = (pkgPrefix == "" || zs.structname.package == "")
                         ? pkgPrefix + zs.structname.package
                         : pkgPrefix + "." + zs.structname.package;

        // Ensure whole package tree
        auto parents = StringUtil::split(package, '.');
        while (parents.size() > 1) {
            auto thispkg = parents.back();
            parents.pop_back();
            auto parent = StringUtil::join(parents, '.');
            packages[parent].first.push_back(thispkg);
        }

        packages[package].second.push_back(&zs);
    }
    // Ensure uniqueness of submodules and structs
    for (auto& kv : packages) {
        {
            auto& submodules = kv.second.first;
            sort(submodules.begin(), submodules.end());
            auto last = unique(submodules.begin(), submodules.end());
            submodules.erase(last, submodules.end());
        }

        {
            auto& structs = kv.second.second;
            sort(structs.begin(), structs.end());
            auto last = unique(structs.begin(), structs.end());
            structs.erase(last, structs.end());
        }
    }

    for (auto& kv : packages) {
        auto& package = kv.first;
        auto& submodules_structs = kv.second;

        if (genPkgFiles) {
            if (!package.empty()) {
                // TODO: outfile checking here is a bit tougher because you need to check all
                //       files beneath the package to see if they've changed.
                EmitJuliaPackage ejPkg {zcm, package};
                int ret = ejPkg.emitPackage(submodules_structs.first,
                                            submodules_structs.second);
                if (ret != 0) return ret;
            }
        } else {
            for (auto* zs : submodules_structs.second) {
                auto outFile = EmitJuliaType::getFilename(zcm, package, zs->structname.shortname);
                if (zcm.needsGeneration(zs->zcmfile, outFile)) {
                    EmitJuliaType ejType {zcm, package, *zs};
                    int ret = ejType.emitType();
                    if (ret != 0) return ret;
                }
            }
        }
    }

    return 0;
}

vector<string> getFilepathsJulia(const ZCMGen& zcm)
{
    vector<string> ret;

    string pkgPrefix = zcm.gopt->getString("julia-pkg-prefix");

    bool genPkgFiles = zcm.gopt->getBool("julia-generate-pkg-files");

    // Map of packages to their submodules and structs
    unordered_map<string, std::pair<vector<string>, vector<const ZCMStruct*>>> packages;
    // Add all stucts
    for (auto& zs : zcm.structs) {
        auto package = (pkgPrefix == "" || zs.structname.package == "")
                         ? pkgPrefix + zs.structname.package
                         : pkgPrefix + "." + zs.structname.package;

        // Ensure whole package tree
        auto parents = StringUtil::split(package, '.');
        while (parents.size() > 1) {
            auto thispkg = parents.back();
            parents.pop_back();
            auto parent = StringUtil::join(parents, '.');
            packages[parent].first.push_back(thispkg);
        }

        packages[package].second.push_back(&zs);
    }
    // Ensure uniqueness of submodules and structs
    for (auto& kv : packages) {
        {
            auto& submodules = kv.second.first;
            sort(submodules.begin(), submodules.end());
            auto last = unique(submodules.begin(), submodules.end());
            submodules.erase(last, submodules.end());
        }

        {
            auto& structs = kv.second.second;
            sort(structs.begin(), structs.end());
            auto last = unique(structs.begin(), structs.end());
            structs.erase(last, structs.end());
        }
    }

    for (auto& kv : packages) {
        auto& package = kv.first;
        auto& submodules_structs = kv.second;

        if (genPkgFiles) {
            if (!package.empty()) ret.push_back(EmitJuliaPackage::getFilename(zcm, package));
        } else {
            for (auto* zs : submodules_structs.second)
                ret.push_back(EmitJuliaType::getFilename(zcm, package, zs->structname.shortname));
        }
    }

    return ret;
}
