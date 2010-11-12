#include <util/io/BufferedInput.h>

NS_IZENELIB_UTIL_BEGIN
namespace io{

BufferedInput::BufferedInput(char* buf,size_t buffsize)
{
    buffer_ = buf;
    bufferSize_ = buffsize;
    bOwnBuff_ = false;
    dirty_ = false;

    bufferStart_ = 0;
    bufferLength_ = 0;
    bufferPosition_ = 0;

    length_ = 0;
}
BufferedInput::BufferedInput(size_t buffsize)
{
    if (buffsize > 0)
    {
        buffer_ = NULL;
        bufferSize_ = buffsize;
    }
    else
    {
        buffer_ = NULL;
        bufferSize_ = BUFFEREDINPUT_BUFFSIZE;
    }

    bOwnBuff_ = true;
    dirty_ = false;

    bufferStart_ = 0;
    bufferLength_ = 0;
    bufferPosition_ = 0;

    length_ = 0;
}

BufferedInput::~BufferedInput(void)
{
    if (bOwnBuff_)
    {
        if (buffer_)
        {
            delete[]buffer_;
            buffer_ = NULL;
        }
    }
}

void BufferedInput::reset()
{
    bufferStart_ = 0;
    bufferLength_ = 0;
    bufferPosition_ = 0;
}

void BufferedInput::read(char* data, size_t length)
{
    if(dirty_)
    {
        throw std::runtime_error(
            "Data dirty.");
    }
    if (bufferPosition_ >= (size_t)bufferStart_)
        refill();
    if (length <= (bufferLength_ - bufferPosition_))
    {
        memcpy(data,buffer_ + bufferPosition_,length);
        bufferPosition_ += length;
    }
    else
    {
        size_t start = bufferLength_ - bufferPosition_;
        if (start > 0)
        {
            memcpy(data,buffer_ + bufferPosition_,start);
        }

        //readInternal(data,start,length - start);
        readInternal(data + start,length - start);
        bufferStart_ += length;
    }
}
void BufferedInput::readBytes(uint8_t* b,size_t len)
{
    if(dirty_)
    {
        throw std::runtime_error(
            "Data dirty.");
    }

    if (len < bufferSize_)
    {
        for (size_t i = 0; i < len; i++)
            // read byte-by-byte
            b[i] = (uint8_t) readByte();
    }
    else
    {
        // read all-at-once
        int64_t start = getFilePointer();
        seekInternal(start);
        readInternal((char*)b,len);

        bufferStart_ = start + len; // adjust stream variables
        bufferPosition_ = 0;
        bufferLength_ = 0; // trigger refill() on read
    }
}
void BufferedInput::readInts(int32_t* i,size_t len)
{
    for (size_t l = 0;l < len;l++)
        i[l] = readInt();
}

void BufferedInput::setBuffer(char* buf,size_t bufSize)
{
    if (bufferPosition_ != 0)
    {
        throw std::runtime_error(
            "void BufferedInput::setBuffer(char* buf,size_t bufSize):you must call setBuffer() before reading any data.");
    }
    if (bOwnBuff_ && buffer_)
    {
        delete[] buffer_;
    }
    buffer_ = buf;
    bufferSize_ = bufSize;
    bOwnBuff_ = false;
}

}

NS_IZENELIB_UTIL_END
