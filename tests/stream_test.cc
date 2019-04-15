#include <pistache/stream.h>

#include "gtest/gtest.h"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

using namespace Pistache;

TEST(stream, test_buffer)
{
    char str[] = "test_string";
    size_t len = strlen(str);
    RawBuffer buffer1(str, len, false);

    RawBuffer buffer2 = buffer1.detach(0);
    ASSERT_EQ(buffer2.size(), len);
    ASSERT_EQ(buffer2.isDetached(), true);

    RawBuffer buffer3;
    ASSERT_EQ(buffer3.size(), 0u);
    ASSERT_EQ(buffer3.isDetached(), false);

    RawBuffer buffer4 = buffer3.detach(0);
    ASSERT_EQ(buffer4.size(), 0u);
    ASSERT_EQ(buffer4.isDetached(), false);
    
    ASSERT_THROW(buffer1.detach(2 * len);, std::range_error);
}

TEST(stream, test_file_buffer)
{
    char fileName[PATH_MAX] = "/tmp/pistacheioXXXXXX";
    if(!mkstemp(fileName))
    {
        std::cerr << "No suitable filename can be generated!" << std::endl;
    }
    std::cout << "Temporary file name: " << fileName << std::endl;

    const std::string dataToWrite("Hello World!");
    std::ofstream tmpFile;
    tmpFile.open(fileName);
    tmpFile << dataToWrite;
    tmpFile.close();

    FileBuffer fileBuffer(fileName);

    ASSERT_NE(fileBuffer.fd(), -1);
    ASSERT_EQ(fileBuffer.size(), dataToWrite.size());

    std::remove(fileName);
}
