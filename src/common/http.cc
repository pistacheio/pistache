/*
 * SPDX-FileCopyrightText: 2015 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* http.cc
   Mathieu Stefani, 13 August 2015

   Http layer implementation
*/

#include <pistache/winornix.h>

#include <pistache/config.h>
#include <pistache/eventmeth.h>
#include <pistache/http.h>
#include <pistache/http_header.h>
#include <pistache/net.h>
#include <pistache/peer.h>
#include <pistache/transport.h>

#include PST_STRERROR_R_HDR

#include <charconv>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <fcntl.h> // for file-constants (_O_RDONLY etc.) in Windows
#include PST_FCNTL_HDR // for function fcntl()

#include PST_MISC_IO_HDR // for _close (io.h / unistd.h)
#include PIST_FILEFNS_HDR // for "open"

#include <sys/stat.h>
#include <sys/types.h>

namespace Pistache::Http
{

    template <typename H, typename Stream, typename... Args>
    typename std::enable_if<Header::IsHeader<H>::value, Stream&>::type
    writeHeader(Stream& stream, Args&&... args)
    {
        H header(std::forward<Args>(args)...);

        stream << H::Name << ": ";
        header.write(stream);

        stream << crlf;

        return stream;
    }

    namespace
    {
        bool writeStatusLine(Version version, Code code, DynamicStreamBuf& buf)
        {
#define PST_OUT(...)      \
    do                    \
    {                     \
        __VA_ARGS__;      \
        if (!os)          \
            return false; \
    } while (0)

            std::ostream os(&buf);

            PST_OUT(os << version << " ");
            PST_OUT(os << static_cast<int>(code));
            PST_OUT(os << ' ');
            PST_OUT(os << code);
            PST_OUT(os << crlf);

            return true;

#undef PST_OUT
        }

        bool writeHeaders(const Header::Collection& headers, DynamicStreamBuf& buf)
        {
#define PST_OUT(...)      \
    do                    \
    {                     \
        __VA_ARGS__;      \
        if (!os)          \
            return false; \
    } while (0)

            std::ostream os(&buf);

            for (const auto& header : headers.list())
            {
                PST_OUT(os << header->name() << ": ");
                PST_OUT(header->write(os));
                PST_OUT(os << crlf);
            }

            return true;

#undef PST_OUT
        }

        bool writeCookies(const CookieJar& cookies, DynamicStreamBuf& buf)
        {
#define PST_OUT(...)      \
    do                    \
    {                     \
        __VA_ARGS__;      \
        if (!os)          \
            return false; \
    } while (0)

            std::ostream os(&buf);
            for (const auto& cookie : cookies)
            {
                PST_OUT(os << "Set-Cookie: ");
                PST_OUT(os << cookie);
                PST_OUT(os << crlf);
            }

            return true;

#undef PST_OUT
        }

        using HttpMethods = std::unordered_map<std::string, Method>;

        const HttpMethods httpMethods = {
#define METHOD(repr, str) { str, Method::repr },
            HTTP_METHODS
#undef METHOD
        };

    } // namespace

    namespace Private
    {

        Step::Step(Message* request)
            : message(request)
        { }

        void Step::raise(const char* msg, Code code /* = Code::Bad_Request */)
        {
            throw HttpError(code, msg);
        }

        State RequestLineStep::apply(StreamCursor& cursor)
        {
            StreamCursor::Revert revert(cursor);

            auto* request = static_cast<Request*>(message);

            StreamCursor::Token methodToken(cursor);
            if (!match_until(' ', cursor))
                return State::Again;

            auto it = httpMethods.find(methodToken.text());
            if (it != httpMethods.end())
            {
                request->method_ = it->second;
            }
            else
            {
                raise("Unknown HTTP request method");
            }

            int n;

            if (cursor.eof())
                return State::Again;
            else if ((n = cursor.current()) != ' ')
                raise("Malformed HTTP request after Method, expected SP");

            if (!cursor.advance(1))
                return State::Again;

            StreamCursor::Token resToken(cursor);
            while ((n = cursor.current()) != '?' && n != ' ')
                if (!cursor.advance(1))
                    return State::Again;

            request->resource_ = resToken.text();

            // Query parameters of the Uri
            if (n == '?')
            {
                if (!cursor.advance(1))
                    return State::Again;

                while ((n = cursor.current()) != ' ')
                {
                    StreamCursor::Token keyToken(cursor);
                    if (!match_until({ '=', ' ', '&' }, cursor))
                        return State::Again;

                    std::string key = keyToken.text();

                    auto c = cursor.current();
                    if (c == ' ')
                    {
                        request->query_.add(std::move(key), "");
                    }
                    else if (c == '&')
                    {
                        request->query_.add(std::move(key), "");
                        if (!cursor.advance(1))
                            return State::Again;
                    }
                    else if (c == '=')
                    {
                        if (!cursor.advance(1))
                            return State::Again;

                        StreamCursor::Token valueToken(cursor);
                        if (!match_until({ ' ', '&' }, cursor))
                            return State::Again;

                        std::string value = valueToken.text();
                        request->query_.add(std::move(key), std::move(value));
                        if (cursor.current() == '&')
                        {
                            if (!cursor.advance(1))
                                return State::Again;
                        }
                    }
                }
            }

            // @Todo: Fragment

            // SP
            if (!cursor.advance(1))
                return State::Again;

            // HTTP-Version
            StreamCursor::Token versionToken(cursor);

            while (!cursor.eol())
                if (!cursor.advance(1))
                    return State::Again;

            const char* ver   = versionToken.rawText();
            const size_t size = versionToken.size();
            if (strncmp(ver, "HTTP/1.0", size) == 0)
            {
                request->version_ = Version::Http10;
            }
            else if (strncmp(ver, "HTTP/1.1", size) == 0)
            {
                request->version_ = Version::Http11;
            }
            else
            {
                raise("Encountered invalid HTTP version");
            }

            if (!cursor.advance(2))
                return State::Again;

            revert.ignore();
            return State::Next;
        }

        State ResponseLineStep::apply(StreamCursor& cursor)
        {
            StreamCursor::Revert revert(cursor);

            auto* response = static_cast<Response*>(message);

            if (match_raw("HTTP/1.1", strlen("HTTP/1.1"), cursor))
            {
                // response->version = Version::Http11;
            }
            else if (match_raw("HTTP/1.0", strlen("HTTP/1.0"), cursor))
            {
            }
            else
            {
                raise("Encountered invalid HTTP version");
            }

            int n;
            // SP
            if ((n = cursor.current()) != StreamCursor::Eof && n != ' ')
                raise("Expected SPACE after http version");
            if (!cursor.advance(1))
                return State::Again;

            StreamCursor::Token codeToken(cursor);
            if (!match_until(' ', cursor))
                return State::Again;

            int code               = 0;
            const char* beg        = codeToken.rawText();
            const char* end        = codeToken.rawText() + codeToken.size();
            const auto parseResult = std::from_chars(beg, end, code);

            if (parseResult.ec != std::errc {} || *parseResult.ptr != ' ')
                raise("Failed to parse return code");
            response->code_ = static_cast<Http::Code>(code);

            if (!cursor.advance(1))
                return State::Again;

            while (!cursor.eol() && !cursor.eof())
            {
                cursor.advance(1);
            }

            if (!cursor.advance(2))
                return State::Again;

            revert.ignore();
            return State::Next;
        }

        State HeadersStep::apply(StreamCursor& cursor)
        {
            StreamCursor::Revert revert(cursor);

            while (!cursor.eol())
            {
                StreamCursor::Revert headerRevert(cursor);

                // Read the header name
                size_t start = cursor;

                while (cursor.current() != ':')
                    if (!cursor.advance(1))
                        return State::Again;

                // Skip the ':'
                if (!cursor.advance(1))
                    return State::Again;

                std::string name = std::string(cursor.offset(start), cursor.diff(start) - 1);

                // Ignore spaces
                while (cursor.current() == ' ')
                    if (!cursor.advance(1))
                        return State::Again;

                // Read the header value
                start = cursor;
                while (!cursor.eol())
                {
                    if (!cursor.advance(1))
                        return State::Again;
                }

                if (Header::LowercaseEqualStatic(name, "cookie"))
                {
                    message->cookies_.removeAllCookies(); // removing existing cookies before
                                                          // re-adding them.
                    message->cookies_.addFromRaw(cursor.offset(start), cursor.diff(start));
                }
                else if (Header::LowercaseEqualStatic(name, "set-cookie"))
                {
                    message->cookies_.add(
                        Cookie::fromRaw(cursor.offset(start), cursor.diff(start)));
                }

                // If the header is registered with the Registry, add its strongly
                //  typed form to the headers list...
                else if (Header::Registry::instance().isRegistered(name))
                {
                    std::shared_ptr<Header::Header> header = Header::Registry::instance().makeHeader(name);
                    header->parseRaw(cursor.offset(start), cursor.diff(start));
                    message->headers_.add(header);
                }

                // But also preserve a raw header version too, regardless of whether
                //  its type was known to the Registry...
                std::string value(cursor.offset(start), cursor.diff(start));
                message->headers_.addRaw(Header::Raw(std::move(name), std::move(value)));

                // CRLF
                if (!cursor.advance(2))
                    return State::Again;

                headerRevert.ignore();
            }

            if (!cursor.advance(2))
                return State::Again;

            revert.ignore();
            return State::Next;
        }

        State BodyStep::apply(StreamCursor& cursor)
        {
            auto cl = message->headers_.tryGet<Header::ContentLength>();
            auto te = message->headers_.tryGet<Header::TransferEncoding>();

            if (cl && te)
                raise("Got mutually exclusive ContentLength and TransferEncoding header");

            if (cl)
                return parseContentLength(cursor, cl);

            if (te)
                return parseTransferEncoding(cursor, te);

            return State::Done;
        }

        State BodyStep::parseContentLength(
            StreamCursor& cursor, const std::shared_ptr<Header::ContentLength>& cl)
        {
            auto contentLength = cl->value();

            auto readBody = [&](size_t size) {
                StreamCursor::Token token(cursor);
                const size_t available = cursor.remaining();

                // We have an incomplete body, read what we can
                if (available < size)
                {
                    cursor.advance(available);
                    message->body_.append(token.rawText(), token.size());

                    bytesRead += available;

                    return false;
                }

                cursor.advance(size);
                message->body_.append(token.rawText(), token.size());
                return true;
            };

            // We already started to read some bytes but we got an incomplete payload
            if (bytesRead > 0)
            {
                // How many bytes do we still need to read ?
                const size_t remaining = static_cast<size_t>(
                    contentLength - bytesRead);
                if (!readBody(remaining))
                    return State::Again;
            }
            // This is the first time we are reading the payload
            else
            {
                message->body_.reserve(
                    static_cast<unsigned int>(contentLength));
                if (!readBody(static_cast<size_t>(contentLength)))
                    return State::Again;
            }

            bytesRead = 0;
            return State::Done;
        }

        BodyStep::Chunk::Result BodyStep::Chunk::parse(StreamCursor& cursor)
        {
            if (size == -1)
            {
                StreamCursor::Revert revert(cursor);
                StreamCursor::Token chunkSize(cursor);

                while (!cursor.eol())
                    if (!cursor.advance(1))
                        return Incomplete;

                const char* raw { chunkSize.rawText() };
                const auto* end { chunkSize.rawText() + chunkSize.size() };

                size_t sz              = 0;
                const auto parseResult = std::from_chars(raw, end, sz, 16);

                if (parseResult.ec != std::errc {} || *parseResult.ptr != '\r')
                    throw std::runtime_error("Invalid chunk size");

                // CRLF
                if (!cursor.advance(2))
                    return Incomplete;

                revert.ignore();

                size                      = sz;
                alreadyAppendedChunkBytes = 0;
            }

            if (size == 0)
                return Final;

            message->body_.reserve(size);
            StreamCursor::Token chunkData(cursor);
            const PST_SSIZE_T available = cursor.remaining();

            if (available + alreadyAppendedChunkBytes < size + 2)
            {
                cursor.advance(available);
                message->body_.append(chunkData.rawText(), available);
                alreadyAppendedChunkBytes += available;
                return Incomplete;
            }
            cursor.advance(size - alreadyAppendedChunkBytes);

            // trailing EOL
            cursor.advance(2);

            message->body_.append(chunkData.rawText(), size - alreadyAppendedChunkBytes);

            return Complete;
        }

        State BodyStep::parseTransferEncoding(
            StreamCursor& cursor, const std::shared_ptr<Header::TransferEncoding>& te)
        {
            auto encoding = te->encoding();
            if (encoding == Http::Header::Encoding::Chunked)
            {
                Chunk::Result result;
                try
                {
                    while ((result = chunk.parse(cursor)) != Chunk::Final)
                    {
                        if (result == Chunk::Incomplete)
                            return State::Again;

                        chunk.reset();
                        if (cursor.eof())
                            return State::Again;
                    }
                    chunk.reset();
                }
                catch (const std::exception& e)
                {
                    // reset chunk incase signal handled & chunk eventually reused
                    chunk.reset();
                    raise(e.what());
                }

                return State::Done;
            }
            else
            {
                raise("Unsupported Transfer-Encoding", Code::Not_Implemented);
            }
            // raise defined with [[noreturn]], so compiler knows we cannot
            // reach here
        }

        ParserBase::ParserBase(size_t maxDataSize)
            : buffer(maxDataSize)
            , cursor(&buffer)
        { }

        State ParserBase::parse()
        {
            State state;
            do
            {
                Step* step = allSteps[currentStep].get();
                state      = step->apply(cursor);
                if (state == State::Next)
                {
                    ++currentStep;
                }
            } while (state == State::Next);

            // Should be either Again or Done
            return state;
        }

        bool ParserBase::feed(const char* data, size_t len)
        {
            return buffer.feed(data, len);
        }

        void ParserBase::reset()
        {
            buffer.reset();
            cursor.reset();

            currentStep = 0;
        }

        Step* ParserBase::step()
        {
            return allSteps[currentStep].get();
        }

    } // namespace Private

    namespace Uri
    {

        Query::Query()
            : params()
        { }

        Query::Query(
            std::initializer_list<std::pair<const std::string, std::string>> params)
            : params(params)
        { }

        void Query::add(std::string name, std::string value)
        {
            params.insert(std::make_pair(std::move(name), std::move(value)));
        }

        std::optional<std::string> Query::get(const std::string& name) const
        {
            auto it = params.find(name);
            if (it == std::end(params))
                return std::nullopt;

            return std::optional<std::string>(it->second);
        }

        std::string Query::as_str() const
        {
            std::string query_url;
            for (const auto& e : params)
            {
                query_url += "&" + e.first + "=" + e.second;
            }
            if (!query_url.empty())
            {
                query_url[0] = '?'; // replace first `&` with `?`
            }
            return query_url;
        }

        bool Query::has(const std::string& name) const
        {
            return params.find(name) != std::end(params);
        }

    } // namespace Uri

    Message::Message(Version version)
        : version_(version)
    { }

    Version Message::version() const { return version_; }

    Code Message::code() const { return code_; }

    const std::string& Message::body() const { return body_; }

    std::string& Message::body() { return body_; }

    const Header::Collection& Message::headers() const { return headers_; }

    Header::Collection& Message::headers() { return headers_; }

    const CookieJar& Message::cookies() const { return cookies_; }

    CookieJar& Message::cookies() { return cookies_; }

    Method Request::method() const { return method_; }

    const std::string& Request::resource() const { return resource_; }

    const Uri::Query& Request::query() const { return query_; }

    const Address& Request::address() const { return address_; }

    std::chrono::milliseconds Request::timeout() const { return timeout_; }

    Header::Encoding Request::getBestAcceptEncoding() const
    {
        const auto& maybe_header = headers().tryGet<Header::AcceptEncoding>();
        if (maybe_header == nullptr)
        {
            return Header::Encoding::Identity;
        }

        const auto& header = *maybe_header;

        for (const auto& encoding : header.encodings())
        {
            // If the qvalue is 0, the encoding is not supported by the client
            if (encodingSupported(encoding.first) && encoding.second != 0)
            {
                return encoding.first;
            }
        }

        return Header::Encoding::Identity;
    }

    Response::Response(Version version)
        : Message(version)
    { }

#ifdef LIBSTDCPP_SMARTPTR_LOCK_FIXME
    std::shared_ptr<Tcp::Peer> Request::peer() const
    {
        auto p = peer_.lock();

        if (!p)
            throw std::runtime_error("Failed to retrieve peer: Broken pipe");

        return p;
    }
#endif

    ResponseStream::ResponseStream(ResponseStream&& other)
        : response_(std::move(other.response_))
        , peer_(std::move(other.peer_))
        , buf_(std::move(other.buf_))
        , transport_(other.transport_)
        , timeout_(std::move(other.timeout_))
    { }

    ResponseStream::ResponseStream(Message&& other, std::weak_ptr<Tcp::Peer> peer,
                                   Tcp::Transport* transport, Timeout timeout,
                                   size_t streamSize, size_t maxResponseSize)
        : response_(std::move(other))
        , peer_(std::move(peer))
        , buf_(streamSize, maxResponseSize)
        , transport_(transport)
        , timeout_(std::move(timeout))
    {
        if (!writeStatusLine(response_.version(), response_.code(), buf_))
            throw Error("Response exceeded buffer size");

        if (!writeCookies(response_.cookies(), buf_))
        {
            throw Error("Response exceeded buffer size");
        }

        if (writeHeaders(response_.headers(), buf_))
        {
            std::ostream os(&buf_);
            /* @Todo @Major:
             * Correctly handle non-keep alive requests
             * Do not put Keep-Alive if version == Http::11 and request.keepAlive ==
             * true
             */
            // writeHeader<Header::Connection>(os, ConnectionControl::KeepAlive);
            // if (!os) throw Error("Response exceeded buffer size");
            writeHeader<Header::TransferEncoding>(os, Header::Encoding::Chunked);
            if (!os)
                throw Error("Response exceeded buffer size");
            os << crlf;
        }
    }

    ResponseStream& ResponseStream::operator=(ResponseStream&& other)
    {
        response_  = std::move(other.response_);
        peer_      = std::move(other.peer_);
        buf_       = std::move(other.buf_);
        transport_ = other.transport_;
        timeout_   = std::move(other.timeout_);

        return *this;
    }

    std::streamsize ResponseStream::write(const char* data, std::streamsize sz)
    {
        std::ostream os(&buf_);
        os << std::hex << sz << crlf;
        os.write(data, sz);
        os << crlf;
        return sz;
    }

    std::shared_ptr<Tcp::Peer> ResponseStream::peer() const
    {
        if (peer_.expired())
        {
            throw std::runtime_error("Write failed: Broken pipe");
        }

        return peer_.lock();
    }

    void ResponseStream::flush()
    {
        timeout_.disarm();
        auto buf = buf_.buffer();

        auto fd = peer()->fd();
        transport_->asyncWrite(fd, buf);
        transport_->flush();

        buf_.clear();
    }

    void ResponseStream::ends()
    {
        std::ostream os(&buf_);
        os << "0" << crlf;
        os << crlf;

        if (!os)
        {
            throw Error("Response exceeded buffer size");
        }

        flush();
    }

    ResponseWriter::ResponseWriter(ResponseWriter&& other)
        : response_(std::move(other.response_))
        , peer_(other.peer_)
        , buf_(std::move(other.buf_))
        , transport_(other.transport_)
        , timeout_(std::move(other.timeout_))
    { }

    ResponseWriter::ResponseWriter(Http::Version version, Tcp::Transport* transport,
                                   Handler* handler, std::weak_ptr<Tcp::Peer> peer)
        : response_(version)
        , peer_(peer)
        , buf_(DefaultStreamSize, handler->getMaxResponseSize())
        , transport_(transport)
        , timeout_(transport, version, handler, peer)
    { }

    ResponseWriter::ResponseWriter(const ResponseWriter& other)
        : response_(other.response_)
        , peer_(other.peer_)
        , buf_(DefaultStreamSize, other.buf_.maxSize())
        , transport_(other.transport_)
        , timeout_(other.timeout_)
    { }

    void ResponseWriter::setMime(const Mime::MediaType& mime)
    {
        auto ct = response_.headers().tryGet<Header::ContentType>();
        if (ct)
        {
            ct->setMime(mime);
        }
        else
        {
            response_.headers().add(std::make_shared<Header::ContentType>(mime));
        }
    }

    Async::Promise<PST_SSIZE_T> ResponseWriter::sendMethodNotAllowed(
        const std::vector<Http::Method>& supportedMethods)
    {
        response_.code_ = Http::Code::Method_Not_Allowed;
        response_.headers().add(
            std::make_shared<Http::Header::Allow>(supportedMethods));
        const std::string& body = codeString(Pistache::Http::Code::Method_Not_Allowed);
        return putOnWire(body.c_str(), body.size());
    }

    Async::Promise<PST_SSIZE_T> ResponseWriter::send(Code code, const std::string& body,
                                                     const Mime::MediaType& mime)
    {
        return sendImpl(code, body.c_str(), body.size(), mime);
    }

    Async::Promise<PST_SSIZE_T> ResponseWriter::send(Code code, const char* data,
                                                     const size_t size,
                                                     const Mime::MediaType& mime)
    {
        return sendImpl(code, data, size, mime);
    }

    Async::Promise<PST_SSIZE_T> ResponseWriter::sendImpl(Code code, const char* data,
                                                         const size_t size,
                                                         const Mime::MediaType& mime)
    {
        if (!peer_.expired())
        {
            auto curPeer = peer_.lock();
            curPeer->setIdle(true); // change peer state to idle

            // It will result in double free
            // Http::Handler::getParser(curPeer)->reset(); // reset the timeout time
        }

        response_.code_ = code;

        if (mime.isValid())
        {
            auto contentType = headers().tryGet<Header::ContentType>();
            if (contentType)
            {
                contentType->setMime(mime);
            }
            else
            {
                headers().add(std::make_shared<Header::ContentType>(mime));
            }
        }

        // Compress data, if necessary, before sending over wire to user...
        switch (contentEncoding_)
        {

#ifdef PISTACHE_USE_CONTENT_ENCODING_BROTLI
        // User requested Brotli compression...
        case Http::Header::Encoding::Br: {

            // Location for size of compressed buffer, initially set to upper
            //  bound on the data after its been compressed...
            size_t compressedSize = ::BrotliEncoderMaxCompressedSize(size);

            // Failed...
            if (compressedSize == 0)
                throw std::runtime_error("BrotliEncoderMaxCompressedSize() failed");

            // Allocate a smart buffer to contain compressed data...
            std::unique_ptr compressedData = std::make_unique<std::byte[]>(compressedSize);

            // Compress data. The encoder expects compressedSize to initially be
            //  the size of the output buffer. After it completes writing it
            //  will update its value to reflect actual size used...
            const auto compressionStatus = ::BrotliEncoderCompress(
                contentEncodingBrotliLevel_,
                BROTLI_DEFAULT_WINDOW,
                BROTLI_DEFAULT_MODE,
                size,
                reinterpret_cast<const uint8_t*>(data),
                &compressedSize,
                reinterpret_cast<uint8_t*>(compressedData.get()));

            // Failed...
            if (compressionStatus != BROTLI_TRUE)
                throw std::runtime_error("BrotliEncoderCompress() failed");

            // Notify client to expect Brotli compressed response...
            headers().add<Http::Header::ContentEncoding>(
                Http::Header::Encoding::Br);

            // Send compressed data back to client...
            return putOnWire(
                reinterpret_cast<const char*>(compressedData.get()),
                compressedSize);
        }
#endif

#ifdef PISTACHE_USE_CONTENT_ENCODING_ZSTD

        case Http::Header::Encoding::Zstd: {
            size_t estimated_size = ZSTD_compressBound(size);
            // Allocate a smart buffer to contain compressed data...
            std::unique_ptr compressedData = std::make_unique<std::byte[]>(estimated_size);

            auto compress_size = ZSTD_compress(reinterpret_cast<void*>(compressedData.get()), estimated_size,
                                               data, size, contentEncodingZstdLevel_);
            if (ZSTD_isError(compress_size))
            {
                throw std::runtime_error(
                    std::string("failed to compress data to ZSTD on ZSTD_compress(), returning: ") + std::to_string(compress_size));
            }
            headers().add<Http::Header::ContentEncoding>(
                Http::Header::Encoding::Zstd);

            // Send compressed data back to client...
            return putOnWire(
                reinterpret_cast<const char*>(compressedData.get()),
                compress_size);
        }

#endif

#ifdef PISTACHE_USE_CONTENT_ENCODING_DEFLATE
        // User requested deflate compression...
        case Http::Header::Encoding::Deflate: {

            // Compute upper bound on size of expected compressed data. This
            //  will be updated by compress2()...
            uLongf compressedSize = static_cast<uLongf>(::compressBound(static_cast<uLong>(size)));

            // Allocate a smart buffer to contain compressed data...
            std::unique_ptr compressedData = std::make_unique<std::byte[]>(compressedSize);

            // Compress user data at requested level...
            const auto compressionStatus = ::compress2(
                reinterpret_cast<unsigned char*>(compressedData.get()),
                &compressedSize,
                reinterpret_cast<const unsigned char*>(data),
                static_cast<uLong>(size),
                contentEncodingDeflateLevel_);

            // Failed...
            if (compressionStatus != Z_OK)
                throw std::runtime_error(
                    std::string("compress2() failed, returning: ") + std::to_string(compressionStatus));

            // Notify client to expect deflate compressed response...
            headers().add<Http::Header::ContentEncoding>(
                Http::Header::Encoding::Deflate);

            // Send compressed data back to client...
            return putOnWire(
                reinterpret_cast<const char*>(compressedData.get()),
                compressedSize);
        }
#endif
        // No compression requested. Send uncompressed data to client...
        case Http::Header::Encoding::Identity:
            return putOnWire(data, size);

        // Unknown...
        default:
            throw std::runtime_error("User requested unknown content encoding.");
        }
    }

    ResponseStream ResponseWriter::stream(Code code, size_t streamSize)
    {
        response_.code_ = code;

        return ResponseStream(std::move(response_), peer_, transport_,
                              std::move(timeout_), streamSize, buf_.maxSize());
    }

    const CookieJar& ResponseWriter::cookies() const { return response_.cookies(); }

    CookieJar& ResponseWriter::cookies() { return response_.cookies(); }

    const Header::Collection& ResponseWriter::headers() const
    {
        return response_.headers();
    }

    Header::Collection& ResponseWriter::headers() { return response_.headers(); }

    Timeout& ResponseWriter::timeout() { return timeout_; }

    std::shared_ptr<Tcp::Peer> ResponseWriter::peer() const
    {
        if (peer_.expired())
        {
            throw std::runtime_error("Write failed: Broken pipe");
        }

        return peer_.lock();
    }

    DynamicStreamBuf* ResponseWriter::rdbuf() { return &buf_; }

    DynamicStreamBuf* ResponseWriter::rdbuf(DynamicStreamBuf* /*other*/)
    {
        throw std::domain_error("Unimplemented");
    }

    ResponseWriter ResponseWriter::clone() const { return ResponseWriter(*this); }

    Async::Promise<PST_SSIZE_T> ResponseWriter::putOnWire(const char* data,
                                                          size_t len)
    {
        try
        {
            std::ostream os(&buf_);

#define PST_OUT(...)                                      \
    do                                                    \
    {                                                     \
        __VA_ARGS__;                                      \
        if (!os)                                          \
        {                                                 \
            return Async::Promise<PST_SSIZE_T>::rejected( \
                Error("Response exceeded buffer size"));  \
        }                                                 \
    } while (0);

            PST_OUT(writeStatusLine(response_.version(), response_.code(), buf_));
            PST_OUT(writeHeaders(response_.headers(), buf_));
            PST_OUT(writeCookies(response_.cookies(), buf_));

            /* @Todo @Major:
             * Correctly handle non-keep alive requests
             * Do not put Keep-Alive if version == Http::11 and request.keepAlive ==
             * true
             */
            // PST_OUT(writeHeader<Header::Connection>(os, ConnectionControl::KeepAlive));
            PST_OUT(writeHeader<Header::ContentLength>(os, len));

            PST_OUT(os << crlf);

            if (len > 0)
            {
                PST_OUT(os.write(data, len));
            }

            auto buffer = buf_.buffer();
            sent_bytes_ += buffer.size();

            timeout_.disarm();

#undef PST_OUT

            auto fd = peer()->fd();

            return transport_->asyncWrite(fd, buffer)
                .then<std::function<Async::Promise<PST_SSIZE_T>(PST_SSIZE_T)>,
                      std::function<void(std::exception_ptr&)>>(
                    [](PST_SSIZE_T data) {
                        return Async::Promise<PST_SSIZE_T>::resolved(data);
                    },

                    [](std::exception_ptr& eptr) {
                        return Async::Promise<PST_SSIZE_T>::rejected(eptr);
                    });
        }
        catch (const std::runtime_error& e)
        {
            return Async::Promise<PST_SSIZE_T>::rejected(e);
        }
    }

    // Compress using the requested content encoding, if supported, before
    //  sending bits to client. User responsible for setting Content-Encoding
    //  header...
    void ResponseWriter::setCompression(const Pistache::Http::Header::Encoding _contentEncoding)
    {
        switch (_contentEncoding)
        {

#ifdef PISTACHE_USE_CONTENT_ENCODING_BROTLI
        // Application requested Brotli compression...
        case Http::Header::Encoding::Br:
            contentEncoding_ = Http::Header::Encoding::Br;
            break;
#endif

#ifdef PISTACHE_USE_CONTENT_ENCODING_ZSTD
        case Http::Header::Encoding::Zstd:
            contentEncoding_ = Http::Header::Encoding::Zstd;
            break;
#endif

#ifdef PISTACHE_USE_CONTENT_ENCODING_DEFLATE
        // Application requested deflate compression...
        case Http::Header::Encoding::Deflate:
            contentEncoding_ = Http::Header::Encoding::Deflate;
            break;
#endif

        // Application requested identity encoding which means no compression...
        case Http::Header::Encoding::Identity:
            contentEncoding_ = Http::Header::Encoding::Identity;
            break;

        // Any other type is not supported...
        default:
            throw std::runtime_error("Unsupported content encoding compression requested.");
        }
    }

    Async::Promise<PST_SSIZE_T> serveFile(ResponseWriter& writer,
                                          const std::string& fileName,
                                          const Mime::MediaType& contentType)
    {
        struct stat sb;

        int fd = PST_FILE_OPEN(fileName.c_str(), PST_O_RDONLY);
        if (fd == -1)
        {
            PST_DECL_SE_ERR_P_EXTRA;
            std::string str_error(PST_STRERROR_R_ERRNO);
            if (errno == ENOENT)
            {
                throw HttpError(Http::Code::Not_Found, std::move(str_error));
            }
            // eles if TODO
            /* @Improvement: maybe could we check for errno here and emit a different
       error message
    */
            else
            {
                throw HttpError(Http::Code::Internal_Server_Error, std::move(str_error));
            }
        }

        int res = ::fstat(fd, &sb);

        PST_FILE_CLOSE(fd); // Done with fd, close before error can be thrown
        if (res == -1)
        {
            throw HttpError(Code::Internal_Server_Error, "");
        }

        auto* buf = writer.rdbuf();

        std::ostream os(buf);

#define PST_OUT(...)                                      \
    do                                                    \
    {                                                     \
        __VA_ARGS__;                                      \
        if (!os)                                          \
        {                                                 \
            return Async::Promise<PST_SSIZE_T>::rejected( \
                Error("Response exceeded buffer size"));  \
        }                                                 \
    } while (0);

        auto setContentType = [&](const Mime::MediaType& contentType) {
            auto& headers = writer.headers();
            auto ct       = headers.tryGet<Header::ContentType>();
            if (ct)
                ct->setMime(contentType);
            else
                headers.add<Header::ContentType>(contentType);
        };

        PST_OUT(writeStatusLine(writer.response_.version(), Http::Code::Ok, *buf));
        if (contentType.isValid())
        {
            setContentType(contentType);
        }
        else
        {
            auto mime = Mime::MediaType::fromFile(fileName.c_str());
            if (mime.isValid())
                setContentType(mime);
        }

        PST_OUT(writeHeaders(writer.headers(), *buf));

        const size_t len = static_cast<size_t>(sb.st_size);

        PST_OUT(writeHeader<Header::ContentLength>(os, len));

        PST_OUT(os << crlf);

        auto* transport = writer.transport_;
        auto peer       = writer.peer();
        auto sockFd     = peer->fd(); // may be PS_FD_EMPTY

        auto buffer = buf->buffer();
        return transport->asyncWrite(sockFd, buffer,
#ifdef _USE_LIBEVENT_LIKE_APPLE
                                     0, // MSG_MORE unsupported in macos sendmsg
                                        // Instead, we set TCP_NOPUSH via
                                        // setsockopt (see "man tcp").
                                     true // use msg_more_style
#else
                                     MSG_MORE
#endif
                                     )
            .then(
                [=](PST_SSIZE_T) {
                    return transport->asyncWrite(sockFd, FileBuffer(fileName));
                },
                Async::Throw);

#undef PST_OUT
    }

    Private::ParserImpl<Http::Request>::ParserImpl(size_t maxDataSize)
        : ParserBase(maxDataSize)
        , request()
        , time_(std::chrono::steady_clock::now())
    {
        allSteps[0] = std::make_unique<RequestLineStep>(&request);
        allSteps[1] = std::make_unique<HeadersStep>(&request);
        allSteps[2] = std::make_unique<BodyStep>(&request);
    }

    void Private::ParserImpl<Http::Request>::reset()
    {
        ParserBase::reset();

        request = Request();
        time_   = std::chrono::steady_clock::now();
    }

    Private::ParserImpl<Http::Response>::ParserImpl(size_t maxDataSize)
        : ParserBase(maxDataSize)
        , response()
    {
        allSteps[0] = std::make_unique<ResponseLineStep>(&response);
        allSteps[1] = std::make_unique<HeadersStep>(&response);
        allSteps[2] = std::make_unique<BodyStep>(&response);
    }

    void Handler::onInput(const char* buffer, size_t len,
                          const std::shared_ptr<Tcp::Peer>& peer)
    {
        PS_TIMEDBG_START_ARGS("input len %u", len);

        auto parser   = getParser(peer);
        auto& request = parser->request;
        try
        {
            if (!parser->feed(buffer, len))
            {
                PS_LOG_DEBUG("parser returned false");

                parser->reset();
                throw HttpError(Code::Request_Entity_Too_Large,
                                "Request exceeded maximum buffer size");
            }

            auto state = parser->parse();

            if (state == Private::State::Done)
            {
                PS_LOG_DEBUG("Creating response");

                ResponseWriter response(request.version(), transport(), this, peer);

#ifdef LIBSTDCPP_SMARTPTR_LOCK_FIXME
                request.associatePeer(peer);
#endif

                request.copyAddress(peer->address());

                auto connection = request.headers().tryGet<Header::Connection>();

                if (connection)
                {
                    PS_LOG_DEBUG("Response connection control");

                    response.headers().add<Header::Connection>(connection->control());
                }
                else
                {
                    PS_LOG_DEBUG("Response connection close");

                    response.headers().add<Header::Connection>(ConnectionControl::Close);
                }

                PS_LOG_DEBUG("Calling peer->setIdle");
                peer->setIdle(false); // change peer state to not idle

                PS_LOG_DEBUG("Calling onRequest");
                onRequest(request, std::move(response));

                PS_LOG_DEBUG("Calling parser->reset");
                parser->reset();
            }
        }
        catch (const HttpError& err)
        {
            PS_LOG_DEBUG("HTTP Error");

            ResponseWriter response(request.version(), transport(), this, peer);
            response.send(static_cast<Code>(err.code()), err.reason());
            parser->reset();
        }

        catch (const std::exception& e)
        {
            PS_LOG_DEBUG("HTTP exception");

            ResponseWriter response(request.version(), transport(), this, peer);
            response.send(Code::Internal_Server_Error, e.what());
            parser->reset();
        }
    }

    void Handler::onConnection(const std::shared_ptr<Tcp::Peer>& peer)
    {
        peer->putData(ParserData, std::make_shared<RequestParser>(maxRequestSize_));
    }

    void Handler::onTimeout(const Request& /*request*/,
                            ResponseWriter response)
    {
        response.send(Code::Request_Timeout);
    }

    Timeout::~Timeout() { disarm(); }

    void Timeout::disarm()
    {
        if (transport && armed)
        {
            transport->disarmTimer(timerFd);
        }
    }

    bool Timeout::isArmed() const { return armed; }

    Timeout::Timeout(Tcp::Transport* transport_, Http::Version version, Handler* handler_,
                     std::weak_ptr<Tcp::Peer> peer_)
        : handler(handler_)
        , version(version)
        , transport(transport_)
        , armed(false)
        , timerFd(PS_FD_EMPTY)
        , peer(peer_)
    { }

    void Timeout::onTimeout(uint64_t /*numWakeup*/)
    {
        auto sp = peer.lock();
        if (!sp)
            return;

        ResponseWriter response(version, transport, handler, peer);
        auto parser         = Handler::getParser(sp);
        const auto& request = parser->request;
        handler->onTimeout(request, std::move(response));
    }

    void Handler::setMaxRequestSize(size_t value) { maxRequestSize_ = value; }

    size_t Handler::getMaxRequestSize() const { return maxRequestSize_; }

    void Handler::setMaxResponseSize(size_t value) { maxResponseSize_ = value; }

    size_t Handler::getMaxResponseSize() const { return maxResponseSize_; }

    std::shared_ptr<RequestParser>
    Handler::getParser(const std::shared_ptr<Tcp::Peer>& peer)
    {
        return std::static_pointer_cast<RequestParser>(peer->getData(ParserData));
    }

} // namespace Pistache::Http
