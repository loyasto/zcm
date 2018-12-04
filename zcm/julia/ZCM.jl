module ZCM

# AbstractZcmType functions
export encode,
       decode,
       getHash
# Zcm functions
export Zcm,
       good,
       errno,
       strerrno,
       subscribe,
       unsubscribe,
       publish,
       pause,
       resume,
       flush,
       start,
       stop,
       handle,
       handle_nonblock,
       set_queue_size,
       LogEvent,
       LogFile,
       read_next_event,
       read_prev_event,
       read_event_at_offset,
       write_event

import Base: flush,
             start,
             unsafe_convert

# Various compatibility fixes to ensure this runs on Julia v0.5 and v0.6
@static if VERSION < v"0.6.0-"
    # this syntax was deprecated in Julia v0.6. This definition has
    # to be wrapped in an include_string to avoid throwing deprecation
    # warnings on v0.6. This can be eliminated when v0.5 support is
    # dropped.
    include_string("abstract AbstractZcmType")

    # takebuf_array was deprecated in favor of take! in v0.6
    _takebuf_array(buf) = takebuf_array(buf)
else
    include_string("abstract type AbstractZcmType end")

    _takebuf_array(buf) = take!(buf)
end

# Function stubs. Autogenerated ZCM types will extend these functions
# with new methods.
function encode(msg::AbstractZcmType) end
function decode(::Type{AbstractZcmType}, data::Vector{UInt8}) end
function getHash(::Type{AbstractZcmType}) end
function _get_hash_recursive(::Type{AbstractZcmType}, parents::Array{String}) end
function _encode_one(msg::AbstractZcmType, buf) end
function _decode_one(::Type{AbstractZcmType}, buf) end
# TODO: would be nice to have getEncodedSize() and _getEncodedSizeNoHash()


# Note: Julia requires that the memory layout of the C structs is consistent
#       between their definitions in zcm headers and this file

# C ptr types for types that we don't need the internals of
module Native

type Zcm
end

type Sub
end

type UvSub
end

immutable RecvBuf
    recv_utime ::Int64
    zcm        ::Ptr{Void}
    # TODO: This makes the assumption that char in C is 8 bits, which is not required to be true
    data       ::Ptr{UInt8}
    data_size  ::UInt32
end

type EventLog
end

type EventLogEvent
    eventnum  ::Int64
    timestamp ::Int64
    chanlen   ::Int32
    datalen   ::Int32
    channel   ::Ptr{UInt8}
    data      ::Ptr{UInt8}
end

end

using .Native: RecvBuf

"""
The Subscription type contains the Julia handler and also all of the
various C pointers to the libuv handlers.
"""
immutable Subscription{F}
    jl_handler::F
    c_handler::Ptr{Void}
    uv_wrapper::Ptr{Native.UvSub}
    uv_handler::Ptr{Void}
    native_sub::Ptr{Native.Sub}
end

type Zcm
    zcm::Ptr{Native.Zcm}
    subscriptions::Vector{Subscription}

    function Zcm(url::AbstractString = "")
        pointer = ccall(("zcm_create", "libzcm"), Ptr{Native.Zcm}, (Cstring,), url);
        instance = new(pointer, Subscription[])
        finalizer(instance, destroy)
        return instance
    end
end

function destroy(zcm::Zcm)
    if zcm.zcm != C_NULL
        ccall(("zcm_destroy", "libzcm"), Void,
              (Ptr{Native.Zcm},), zcm)
        zcm.zcm = C_NULL
    end
end

# Defines the conversion when we pass this to a C function expecting a pointer
unsafe_convert(::Type{Ptr{Native.Zcm}}, zcm::Zcm) = zcm.zcm

function good(zcm::Zcm)
    (zcm.zcm != C_NULL) && (errno(zcm) == 0)
end

function errno(zcm::Zcm)
    ccall(("zcm_errno", "libzcm"), Cint, (Ptr{Native.Zcm},), zcm)
end

function strerror(zcm::Zcm)
    val =  ccall(("zcm_strerror", "libzcm"), Cstring, (Ptr{Native.Zcm},), zcm)
    if (val == C_NULL)
        return "unable to get strerror"
    else
        return unsafe_string(val)
    end
end

function handler_wrapper(rbuf::Native.RecvBuf, channelbytes::Cstring, handler)
    channel = unsafe_string(channelbytes)
    msgdata = unsafe_wrap(Vector{UInt8}, rbuf.data, rbuf.data_size)
    handler(rbuf, channel, msgdata)
    return nothing
end

function typed_handler{T <: AbstractZcmType}(handler, msgtype::Type{T}, args...)
    (rbuf, channel, msgdata) -> handler(rbuf, channel, decode(msgtype, msgdata), args...)
end

function typed_handler(handler, msgtype::Type{Void}, args...)
    (rbuf, channel, msgdata) -> handler(rbuf, channel, msgdata, args...)
end

"""
    subscribe(zcm::Zcm, channel::AbstractString, handler, additional_args...)

Adds a subscription using ZCM object `zcm` on the given channel. The `handler`
must be a function or callable object, and will be called with:

    handler(rbuf::RecvBuf, channel::String, msgdata::Vector{UInt8})

If additional arguments are supplied to `subscribe()` after the handler,
then they will also be passed to the handler each time it is called. So:

    subscribe(zcm, channel, handler, X, Y, Z)

will cause `handler()` to be invoked with:

    handler(rbuf, channel, msgdata, X, Y, Z)
"""
function subscribe(zcm::Zcm, channel::AbstractString,
                   handler,
                   msgtype=Void,
                   additional_args...)
    callback = typed_handler(handler, msgtype, additional_args...)
    c_handler = cfunction(handler_wrapper, Void,
                          (Ref{Native.RecvBuf}, Cstring, Ref{typeof(callback)}))
    uv_wrapper = ccall(("uv_zcm_msg_handler_create", "libzcmjulia"),
                       Ptr{Native.UvSub},
                       (Ptr{Void}, Ptr{Void}),
                       c_handler, Ref(callback))
    uv_handler = cglobal(("uv_zcm_msg_handler_trigger", "libzcmjulia"))
    try_sub = () -> ccall(("zcm_try_subscribe", "libzcm"), Ptr{Native.Sub},
                          (Ptr{Native.Zcm}, Cstring, Ptr{Void}, Ptr{Native.UvSub}),
                          zcm, channel, uv_handler, uv_wrapper)
    csub = Ptr{Native.Sub}(C_NULL)
    while (true)
        csub = try_sub()
        if (csub == C_NULL)
            yield()
        else
            break
        end
    end
    sub = Subscription(callback, c_handler, uv_wrapper, uv_handler, csub)
    push!(zcm.subscriptions, sub)
    return sub
end

function unsubscribe(zcm::Zcm, sub::Subscription)
    try_unsub = () -> ccall(("zcm_try_unsubscribe", "libzcm"), Cint,
                            (Ptr{Native.Zcm}, Ptr{Native.Sub}), zcm, sub.native_sub)
    ret = Cint(0)
    while (true)
        ret = try_unsub()
        if (ret == -2)
            yield()
        else
            break
        end
    end
    ccall(("uv_zcm_msg_handler_destroy", "libzcmjulia"), Void,
          (Ptr{Native.UvSub},), sub.uv_wrapper)
    deleteat!(zcm.subscriptions, findin(zcm.subscriptions, [sub]))
    return ret
end

function publish(zcm::Zcm, channel::AbstractString, data::Vector{UInt8})
    return ccall(("zcm_publish", "libzcm"), Cint,
                 (Ptr{Native.Zcm}, Cstring, Ptr{Void}, UInt32),
                 zcm, convert(String, channel), data, length(data))
end

function publish(zcm::Zcm, channel::AbstractString, msg::AbstractZcmType)
    publish(zcm, channel, encode(msg))
end

function pause(zcm::Zcm)
    ccall(("zcm_pause", "libzcm"), Void, (Ptr{Native.Zcm},), zcm)
end

function resume(zcm::Zcm)
    ccall(("zcm_resume", "libzcm"), Void, (Ptr{Native.Zcm},), zcm)
end

function flush(zcm::Zcm)
    while (true)
        ret = ccall(("zcm_try_flush", "libzcm"), Cint, (Ptr{Native.Zcm},), zcm)
        if (ret == Cint(0))
            break
        else
            yield()
        end
    end
end

function start(zcm::Zcm)
    ccall(("zcm_start", "libzcm"), Void, (Ptr{Native.Zcm},), zcm)
end

function stop(zcm::Zcm)
    while (true)
        ret = ccall(("zcm_try_stop", "libzcm"), Cint, (Ptr{Native.Zcm},), zcm)
        if (ret == Cint(0))
            break
        else
            yield()
        end
    end
end

function handle(zcm::Zcm)
    ccall(("zcm_handle", "libzcm"), Cint, (Ptr{Native.Zcm},), zcm)
end

function handle_nonblock(zcm::Zcm)
    ccall(("zcm_handle_nonblock", "libzcm"), Cint, (Ptr{Native.Zcm},), zcm)
end

function set_queue_size(zcm::Zcm, num::Integer)
    sz = UInt32(num)
    while (true)
        ret = ccall(("zcm_try_set_queue_size", "libzcm"), Cint, (Ptr{Native.Zcm}, UInt32), zcm, sz)
        if (ret == Cint(0))
            break
        else
            yield()
        end
    end
end

type LogEvent
    # These values only valid for events read from a log
    event   ::Ptr{Native.EventLogEvent}
    num     ::Int64

    # These values valid for user created events or events read from a log
    utime   ::Int64
    channel ::String
    data    ::Array{UInt8}

    # Bookkeeping
    valid   ::Bool

    function LogEvent(channel::AbstractString, msg::AbstractZcmType, utime::Int64)
        instance = new()

        instance.event   = C_NULL
        instance.num     = 0
        instance.utime   = utime
        instance.channel = convert(String, channel)
        instance.data    = encode(msg)
        instance.valid   = true

        finalizer(instance, destroy)

        return instance
    end

    function LogEvent(event::Ptr{Native.EventLogEvent})
        instance = new()
        instance.event = event
        instance.valid = false

        if (event != C_NULL)
            loadedEvent = unsafe_load(event)

            instance.num   = loadedEvent.eventnum
            instance.utime = loadedEvent.timestamp
            if (loadedEvent.channel != C_NULL)
                instance.channel = unsafe_string(loadedEvent.channel, loadedEvent.chanlen)
            else
                instance.channel = ""
            end
            if (loadedEvent.data != C_NULL)
                instance.data    = unsafe_wrap(Array, loadedEvent.data, loadedEvent.datalen)
            else
                instance.data    = []
            end

            instance.valid = true
        end

        # user can force cleanup of their instance by calling `finalize(zcm)`
        finalizer(instance, destroy)

        return instance
    end
end

function destroy(event::LogEvent)
    if (event.event != C_NULL)
        ccall(("zcm_eventlog_free_event", "libzcm"), Void,
              (Ptr{Native.EventLogEvent},), event.event)
        event.event = C_NULL
    end
end

function good(event::LogEvent)
    return event.valid
end

type LogFile
    eventLog::Ptr{Native.EventLog}

    """
    path = the filesystem path of the log
    mode = "w", "r", or "a" for write, read, or append respectively
    """
    function LogFile(path::AbstractString, mode::AbstractString)
        instance = new()
        instance.eventLog = ccall(("zcm_eventlog_create", "libzcm"), Ptr{Native.EventLog},
                                  (Cstring, Cstring), path, mode)

        # user can force cleanup of their instance by calling `finalize(zcm)`
        finalizer(instance, destroy)

        return instance
    end
end

function destroy(log::LogFile)
    if (log.eventLog != C_NULL)
        ccall(("zcm_eventlog_destroy", "libzcm"), Void,
              (Ptr{Native.EventLog},), log.eventLog)
        log.eventLog = C_NULL
    end
end

function good(lf::LogFile)
    return lf.eventLog != C_NULL
end

# TODO: we could actually make all zcmtypes register their hash somewhere and allow the
#       read_event functions to return an actual zcmtype
function read_next_event(lf::LogFile)
    event = ccall(("zcm_eventlog_read_next_event", "libzcm"), Ptr{Native.EventLogEvent},
                  (Ptr{Native.EventLog},), lf.eventLog)
    return LogEvent(event)
end

function read_prev_event(lf::LogFile)
    event = ccall(("zcm_eventlog_read_prev_event", "libzcm"), Ptr{Native.EventLogEvent},
                  (Ptr{Native.EventLog},), lf.eventLog)
    return LogEvent(event)
end

function read_event_at_offset(lf::LogFile, offset::Int64)
    event = ccall(("zcm_eventlog_read_event_at_offset", "libzcm"), Ptr{Native.EventLogEvent},
                  (Ptr{Native.EventLog}, Int64), lf.eventLog, offset)
    return LogEvent(event)
end

function write_event(lf::LogFile, event::LogEvent)
    nativeEvent = Native.EventLogEvent(event.num,
                                       event.utime,
                                       length(event.channel),
                                       length(event.data),
                                       pointer(event.channel),
                                       pointer(event.data))
    return ccall(("zcm_eventlog_write_event", "libzcm"), Cint,
                 (Ptr{Native.EventLog}, Ref{Native.EventLogEvent}),
                 lf.eventLog, nativeEvent)
end

end # module ZCM

