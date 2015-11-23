/** THIS IS AN AUTOMATICALLY GENERATED FILE.  DO NOT MODIFY
 * BY HAND!!
 *
 * Generated by zcm-gen
 **/

#include <zcm/zcm_coretypes.h>

#ifndef __array_const_size1_hpp__
#define __array_const_size1_hpp__

#include <string>


class array_const_size1
{
    public:
        int8_t     i8[1];

        int16_t    i16[2];

        int32_t    i32[3];

        int64_t    i64[4];

        float      f[5];

        double     d[6];

        std::string s[7];

        int8_t     bl[8];

        uint8_t    b[9];

    public:
        /**
         * Encode a message into binary form.
         *
         * @param buf The output buffer.
         * @param offset Encoding starts at thie byte offset into @p buf.
         * @param maxlen Maximum number of bytes to write.  This should generally be
         *  equal to getEncodedSize().
         * @return The number of bytes encoded, or <0 on error.
         */
        inline int encode(void *buf, int offset, int maxlen) const;

        /**
         * Check how many bytes are required to encode this message.
         */
        inline int getEncodedSize() const;

        /**
         * Decode a message from binary form into this instance.
         *
         * @param buf The buffer containing the encoded message.
         * @param offset The byte offset into @p buf where the encoded message starts.
         * @param maxlen The maximum number of bytes to reqad while decoding.
         * @return The number of bytes decoded, or <0 if an error occured.
         */
        inline int decode(const void *buf, int offset, int maxlen);

        /**
         * Retrieve the 64-bit fingerprint identifying the structure of the message.
         * Note that the fingerprint is the same for all instances of the same
         * message type, and is a fingerprint on the message type definition, not on
         * the message contents.
         */
        inline static int64_t getHash();

        /**
         * Returns "array_const_size1"
         */
        inline static const char* getTypeName();

        // ZCM support functions. Users should not call these
        inline int _encodeNoHash(void *buf, int offset, int maxlen) const;
        inline int _getEncodedSizeNoHash() const;
        inline int _decodeNoHash(const void *buf, int offset, int maxlen);
        inline static uint64_t _computeHash(const __zcm_hash_ptr *p);
};

int array_const_size1::encode(void *buf, int offset, int maxlen) const
{
    int pos = 0, tlen;
    int64_t hash = (int64_t)getHash();

    tlen = __int64_t_encode_array(buf, offset + pos, maxlen - pos, &hash, 1);
    if(tlen < 0) return tlen; else pos += tlen;

    tlen = this->_encodeNoHash(buf, offset + pos, maxlen - pos);
    if (tlen < 0) return tlen; else pos += tlen;

    return pos;
}

int array_const_size1::decode(const void *buf, int offset, int maxlen)
{
    int pos = 0, thislen;

    int64_t msg_hash;
    thislen = __int64_t_decode_array(buf, offset + pos, maxlen - pos, &msg_hash, 1);
    if (thislen < 0) return thislen; else pos += thislen;
    if (msg_hash != getHash()) return -1;

    thislen = this->_decodeNoHash(buf, offset + pos, maxlen - pos);
    if (thislen < 0) return thislen; else pos += thislen;

    return pos;
}

int array_const_size1::getEncodedSize() const
{
    return 8 + _getEncodedSizeNoHash();
}

int64_t array_const_size1::getHash()
{
    static int64_t hash = _computeHash(NULL);
    return hash;
}

const char* array_const_size1::getTypeName()
{
    return "array_const_size1";
}

int array_const_size1::_encodeNoHash(void *buf, int offset, int maxlen) const
{
    int pos = 0, tlen;

    tlen = __int8_t_encode_array(buf, offset + pos, maxlen - pos, &this->i8[0], 1);
    if(tlen < 0) return tlen; else pos += tlen;

    tlen = __int16_t_encode_array(buf, offset + pos, maxlen - pos, &this->i16[0], 2);
    if(tlen < 0) return tlen; else pos += tlen;

    tlen = __int32_t_encode_array(buf, offset + pos, maxlen - pos, &this->i32[0], 3);
    if(tlen < 0) return tlen; else pos += tlen;

    tlen = __int64_t_encode_array(buf, offset + pos, maxlen - pos, &this->i64[0], 4);
    if(tlen < 0) return tlen; else pos += tlen;

    tlen = __float_encode_array(buf, offset + pos, maxlen - pos, &this->f[0], 5);
    if(tlen < 0) return tlen; else pos += tlen;

    tlen = __double_encode_array(buf, offset + pos, maxlen - pos, &this->d[0], 6);
    if(tlen < 0) return tlen; else pos += tlen;

    for (int a0 = 0; a0 < 7; a0++) {
        char* __cstr = (char*) this->s[a0].c_str();
        tlen = __string_encode_array(buf, offset + pos, maxlen - pos, &__cstr, 1);
        if(tlen < 0) return tlen; else pos += tlen;
    }

    tlen = __boolean_encode_array(buf, offset + pos, maxlen - pos, &this->bl[0], 8);
    if(tlen < 0) return tlen; else pos += tlen;

    tlen = __byte_encode_array(buf, offset + pos, maxlen - pos, &this->b[0], 9);
    if(tlen < 0) return tlen; else pos += tlen;

    return pos;
}

int array_const_size1::_decodeNoHash(const void *buf, int offset, int maxlen)
{
    int pos = 0, tlen;

    tlen = __int8_t_decode_array(buf, offset + pos, maxlen - pos, &this->i8[0], 1);
    if(tlen < 0) return tlen; else pos += tlen;

    tlen = __int16_t_decode_array(buf, offset + pos, maxlen - pos, &this->i16[0], 2);
    if(tlen < 0) return tlen; else pos += tlen;

    tlen = __int32_t_decode_array(buf, offset + pos, maxlen - pos, &this->i32[0], 3);
    if(tlen < 0) return tlen; else pos += tlen;

    tlen = __int64_t_decode_array(buf, offset + pos, maxlen - pos, &this->i64[0], 4);
    if(tlen < 0) return tlen; else pos += tlen;

    tlen = __float_decode_array(buf, offset + pos, maxlen - pos, &this->f[0], 5);
    if(tlen < 0) return tlen; else pos += tlen;

    tlen = __double_decode_array(buf, offset + pos, maxlen - pos, &this->d[0], 6);
    if(tlen < 0) return tlen; else pos += tlen;

    for (int a0 = 0; a0 < 7; a0++) {
        int32_t __elem_len;
        tlen = __int32_t_decode_array(buf, offset + pos, maxlen - pos, &__elem_len, 1);
        if(tlen < 0) return tlen; else pos += tlen;
        if(__elem_len > maxlen - pos) return -1;
        this->s[a0].assign(((const char*)buf) + offset + pos, __elem_len -  1);
        pos += __elem_len;
    }

    tlen = __boolean_decode_array(buf, offset + pos, maxlen - pos, &this->bl[0], 8);
    if(tlen < 0) return tlen; else pos += tlen;

    tlen = __byte_decode_array(buf, offset + pos, maxlen - pos, &this->b[0], 9);
    if(tlen < 0) return tlen; else pos += tlen;

    return pos;
}

int array_const_size1::_getEncodedSizeNoHash() const
{
    int enc_size = 0;
    enc_size += __int8_t_encoded_array_size(NULL, 1);
    enc_size += __int16_t_encoded_array_size(NULL, 2);
    enc_size += __int32_t_encoded_array_size(NULL, 3);
    enc_size += __int64_t_encoded_array_size(NULL, 4);
    enc_size += __float_encoded_array_size(NULL, 5);
    enc_size += __double_encoded_array_size(NULL, 6);
    for (int a0 = 0; a0 < 7; a0++) {
        enc_size += this->s[a0].size() + 4 + 1;
    }
    enc_size += __boolean_encoded_array_size(NULL, 8);
    enc_size += __byte_encoded_array_size(NULL, 9);
    return enc_size;
}

uint64_t array_const_size1::_computeHash(const __zcm_hash_ptr *)
{
    uint64_t hash = (uint64_t)0xbb93d1c57076cddbLL;
    return (hash<<1) + ((hash>>63)&1);
}

#endif
