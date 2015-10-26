/*  Sirikata Jpeg Reader -- Texture Transfer management system
 *  MuxReader.hpp
 *
 *  Copyright (c) 2015, Daniel Reiter Horn
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _SIRIKATA_MUX_READER_HPP_
#define _SIRIKATA_MUX_READER_HPP_

#include "Allocator.hh"
#include "Error.hh"
#include "Reader.hh"
namespace Sirikata {
class SIRIKATA_EXPORT MuxReader {
    typedef Sirikata::DecoderReader Reader;
    Reader *mReader;
    static JpegError ReadFull(Reader* r, uint8_t *buffer, uint32_t len) {
        while (len != 0) {
            std::pair<uint32, JpegError> ret = r->Read(buffer, len);
            if (ret.first == 0) {
                assert(ret.second != JpegError::nil() && "Read of 0 bytes == error");
                return ret.second; // must have error
            }
            buffer += ret.first;
            len -= ret.first;
        }
        return JpegError::nil();
    }

    JpegError fillBufferOnce(std::vector<uint8_t, JpegAllocator<uint8_t> >&incomingBuffer) {
        uint8_t header[4] = {0, 0, 0};
        JpegError err = ReadFull(mReader, header, 3);
        if (err != JpegError::nil()) {
            return err;
        }
        uint8_t stream_id = 0xf & header[0];
        assert(stream_id < MAX_STREAM_ID && "Stream Id Must be within range");
        if (stream_id >= MAX_STREAM_ID) {
            return JpegError::errMissingFF00();
        }
        std::vector<uint8_t, JpegAllocator<uint8_t> > *buffer = &mBuffer[stream_id];
        uint8_t flags = (header[0] >> 4) & 3;
        size_t offset = buffer->size();
        uint32_t len;
        if (flags == 0) {
            len = header[2];
            len *= 0x100;
            len += header[1] + 1;
            buffer->resize(offset + len);
        } else {
            len = (1024 << (2 * flags));
            buffer->resize(offset + len);
            (*buffer)[offset] = header[1];
            (*buffer)[offset + 1] = header[2];
            len -= 2;
            offset += 2;
        }
        return ReadFull(mReader, buffer->data() + offset, len);
    }
 public:
    enum {MAX_STREAM_ID = 16};
    std::vector<uint8_t, JpegAllocator<uint8_t> > mBuffer[MAX_STREAM_ID];
    uint32_t mOffset[MAX_STREAM_ID];
    bool eof;
    MuxReader(const JpegAllocator<uint8_t> &alloc,
              int num_stream_hint = 4, int stream_hint_reserve_size=65536, Reader *reader = NULL)
        : mReader(reader) {
        eof = false;
        for (int i = 0; i < MAX_STREAM_ID; ++i) { // assign a better allocator
            mBuffer[i] = std::vector<uint8_t, JpegAllocator<uint8_t> > (alloc);
            if (i < num_stream_hint) {
                mBuffer[i].reserve(stream_hint_reserve_size); // prime some of the vectors
            }
            mOffset[i] = 0;
        }
    }
    void init (Reader *reader){
        mReader = reader;
    }
    void fillBufferEntirely(std::pair<std::vector<uint8_t,
                                            JpegAllocator<uint8_t> >::const_iterator,
                                       std::vector<uint8_t,
                                            JpegAllocator<uint8_t> >::const_iterator>* ret) {
        bool all_error = false;
        std::vector<uint8_t, JpegAllocator<uint8_t> > ib;
        while (!all_error) {
            all_error = true;
            for (int i = 0; i < MAX_STREAM_ID; ++i) {
                if (fillBufferOnce(ib) == JpegError::nil()) {
                    all_error = false;
                }
            }
        }
        for (int i = 0; i < MAX_STREAM_ID; ++i) {
            ret[i].first=mBuffer[i].begin() + mOffset[i];
            ret[i].second = mBuffer[i].end();
        }
    }
    JpegError fillBufferUntil(uint8_t desired_stream_id) {
        if (eof) {
            return JpegError::errEOF();
        }
        assert(mOffset[desired_stream_id] == mBuffer[desired_stream_id].size());
        mOffset[desired_stream_id] = 0;
        std::vector<uint8_t, JpegAllocator<uint8_t> > incomingBuffer(mBuffer[desired_stream_id].get_allocator());
        incomingBuffer.swap(mBuffer[desired_stream_id]);
        do {
            JpegError err = JpegError::nil();
            if ((err = fillBufferOnce(incomingBuffer)) != JpegError::nil()) {
                return err;
            }
        } while(mOffset[desired_stream_id] == mBuffer[desired_stream_id].size());
        return JpegError::nil();
    }
    std::pair<uint32, JpegError> Read(uint8_t stream_id, uint8*data, unsigned int size) {
        assert(stream_id < MAX_STREAM_ID && "Invalid stream Id; must be less than 16");
        std::pair<uint32, JpegError> retval(0, JpegError::nil());
        bool bytes_available = mOffset[stream_id] != mBuffer[stream_id].size();
        if (bytes_available || (retval.second = fillBufferUntil(stream_id)) == JpegError::nil()) {
            retval.first = std::min((uint32_t)mBuffer[stream_id].size() - mOffset[stream_id],
                                    size);
            std::memcpy(data, &mBuffer[stream_id][mOffset[stream_id]], retval.first);
            mOffset[stream_id] += retval.first;
        }
        return retval;
    }
    ~MuxReader(){}
};

class SIRIKATA_EXPORT MuxWriter {
    typedef Sirikata::DecoderWriter Writer;
    Writer *mWriter;
public:
    enum {MAX_STREAM_ID = MuxReader::MAX_STREAM_ID};
    enum {MIN_OFFSET = 3};
    enum {MAX_BUFFER_LAG = 65537};
    std::vector<uint8_t, JpegAllocator<uint8_t> > mBuffer[MAX_STREAM_ID];
    uint32_t mOffset[MAX_STREAM_ID];
    uint32_t mFlushed[MAX_STREAM_ID];
    uint32_t mTotalWritten;
    uint32_t mLowWaterMark[MAX_STREAM_ID];
    MuxWriter(Writer* writer, const JpegAllocator<uint8_t> &alloc)
        : mWriter(writer) {
        for (uint8_t i = 0; i < MAX_STREAM_ID; ++i) { // assign a better allocator
            mBuffer[i] = std::vector<uint8_t, JpegAllocator<uint8_t> >(alloc);
            mOffset[i] = 0;
            mFlushed[i] = 0;
            mLowWaterMark[i] = 0;
        }
        mTotalWritten = 0;
    }
    uint32_t highWaterMark(uint32_t flushed) {
        if (flushed & 0xffffc000) {
            return 65536;
        }
        if (flushed & 0xfffff000) {
            return 16384;
        }
        return 4096;
    }

    JpegError flushFull(uint8_t stream_id, uint32_t toBeFlushed) {
        if (toBeFlushed ==0) {
            return JpegError::nil();
        }
        assert(toBeFlushed + mOffset[stream_id] == mBuffer[stream_id].size());
        std::pair<uint32_t, JpegError> retval(0, JpegError::nil());
        do{
            uint32_t offset = mOffset[stream_id];
            assert(offset >= MIN_OFFSET);
            uint32_t toWrite = std::min(toBeFlushed, (uint32_t)65536U);
            mBuffer[stream_id][offset - MIN_OFFSET] = stream_id;
            mBuffer[stream_id][offset - MIN_OFFSET + 1] = ((toWrite - 1) & 0xff);
            mBuffer[stream_id][offset - MIN_OFFSET + 2] = (((toWrite - 1) >> 8) & 0xff);
            retval = mWriter->Write(&mBuffer[stream_id][offset - MIN_OFFSET],
                                   toWrite + MIN_OFFSET);
            assert((retval.first == toWrite + MIN_OFFSET || retval.second != JpegError::nil())
                   && "Writers must write full");
            if (retval.second == JpegError::nil()) {
                mTotalWritten += toWrite;
                mFlushed[stream_id] += toWrite;
                mOffset[stream_id] += toWrite;
                toBeFlushed -= toWrite;
            } else {
                break;
            }
        }while(toBeFlushed > 0);
        mOffset[stream_id] = MIN_OFFSET;
        mBuffer[stream_id].resize(MIN_OFFSET);
        mLowWaterMark[stream_id] = mTotalWritten;
        return retval.second;
    }

    JpegError flushPartial(uint8_t stream_id, uint32_t toBeFlushed) {
        uint8_t code = stream_id;
        uint32_t len = 0;
        if (toBeFlushed < 4096) {
            assert(false && "We shouldn't reach this");
            return flushFull(stream_id, toBeFlushed);
        }
        
        if (toBeFlushed < 16384) {
            if (toBeFlushed > 8192) {
                return flushFull(stream_id, toBeFlushed);
            }
            len = 4096;
            code |= (1 << 4);
        } else if (toBeFlushed < 65536) {
            if (toBeFlushed > 32768) {
                return flushFull(stream_id, toBeFlushed);
            }
            len = 16384;
            code |= (2 << 4);
        } else {
            if (toBeFlushed > 131072) {
                return flushFull(stream_id, toBeFlushed);
            }
            len = 65536;
            code |= (3 << 4);
        }
        std::pair<uint32_t, JpegError> retval(0, JpegError::nil());
        for (uint32_t toWrite = 0; toWrite + len <= toBeFlushed; toWrite += len) {
            uint32_t offset = mOffset[stream_id];
            if (offset == mBuffer[stream_id].size()) continue;
            assert(offset >= MIN_OFFSET);
            mBuffer[stream_id][offset - 1] = code;
            retval = mWriter->Write(&mBuffer[stream_id][offset - 1],
                                   len + 1);
            if (retval.first != len + 1) {
                return retval.second;
            }
            mTotalWritten += len;
            mFlushed[stream_id] += len;
            mOffset[stream_id] += len;
            if (mOffset[stream_id] > 65539) {
                for (std::vector<uint8_t, JpegAllocator<uint8_t> >::iterator
                         src = mBuffer[stream_id].begin() + mOffset[stream_id],
                         dst = mBuffer[stream_id].begin() + MIN_OFFSET,
                         ed = mBuffer[stream_id].end(); src != ed; ++src, ++dst) {
                    *dst = *src;
                }
                mBuffer[stream_id].resize(MIN_OFFSET + mBuffer[stream_id].size() - mOffset[stream_id]);
                mOffset[stream_id] = MIN_OFFSET;
            }
        }
        uint32_t delta = mBuffer[stream_id].size() - mOffset[stream_id];
        if (delta > mTotalWritten) {
            mLowWaterMark[stream_id] = 0;
        } else {
            // we're already delta behind the ground truth
            mLowWaterMark[stream_id] = mTotalWritten - delta;
        }
        return retval.second;
    }
    JpegError flush(uint8_t stream_id) {
        JpegError retval = JpegError::nil();
        for (uint8_t i= 0; i < MAX_STREAM_ID; ++i) {
            uint32_t toBeFlushed = mBuffer[i].size() - mOffset[i];
            if (i == stream_id || !toBeFlushed) {
                continue;
            }
            bool isUrgent = mTotalWritten - mLowWaterMark[i] > MAX_BUFFER_LAG;
            if (toBeFlushed < 4096) {
                if (isUrgent) {
                    // we need to flush what we have
                    retval = flushFull(i, toBeFlushed);
                    assert(mTotalWritten == mLowWaterMark[i]);
                }
            } else {
                if (isUrgent && toBeFlushed < 16384) {
                    retval = flushFull(i, toBeFlushed);
                } else {
                    retval = flushPartial(i, toBeFlushed);
                }
            }
        }
        uint32_t toBeFlushed = mBuffer[stream_id].size() - mOffset[stream_id];
        retval = flushPartial(stream_id, toBeFlushed);
        return retval;
    }
    std::pair<uint32, JpegError> Write(uint8_t stream_id, const uint8*data, unsigned int size) {
        std::pair<uint32, JpegError> retval(size, JpegError::nil());
        size_t bufferSize = mBuffer[stream_id].size();
        if (bufferSize == 0) {
            mBuffer[stream_id].reserve(16387);
            mBuffer[stream_id].resize(MIN_OFFSET);
            mOffset[stream_id] = MIN_OFFSET;
            bufferSize = MIN_OFFSET;
        }
        mBuffer[stream_id].insert(mBuffer[stream_id].end(), data, data + size);
        bufferSize += size;
        uint32_t hwm = highWaterMark(mFlushed[stream_id]);
        if (bufferSize >= mOffset[stream_id] + hwm) {
            retval.second = flush(stream_id);
        }
        return retval;
    }
    void Close() {
        for (uint8_t i = 0; i < MAX_STREAM_ID; ++i) {
            if(mOffset[i] != mBuffer[i].size()) {
                assert(mBuffer[i].size() - mOffset[i] < 65536);
                flushFull(i, mBuffer[i].size() - mOffset[i]);
            }
        }
    }
    ~MuxWriter(){}
};
}
#endif
