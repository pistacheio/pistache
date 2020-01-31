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

TEST(stream, test_buffer) {
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

TEST(stream, test_file_buffer) {
  char fileName[PATH_MAX] = "/tmp/pistacheioXXXXXX";
  if (!mkstemp(fileName)) {
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

TEST(stream, test_dyn_buffer) {
  DynamicStreamBuf buf(128);

  {
    std::ostream os(&buf);

    for (unsigned i = 0; i < 128; ++i) {
      os << "A";
    }
  }

  auto rawbuf = buf.buffer();

  ASSERT_EQ(rawbuf.size(), 128u);
  ASSERT_EQ(rawbuf.isDetached(), false);
  ASSERT_EQ(rawbuf.data().size(), 128u);
  ASSERT_EQ(strlen(rawbuf.data().c_str()), 128u);
}

TEST(stream, test_cursor_advance_for_array) {
  ArrayStreamBuf<char> buffer;
  StreamCursor cursor{&buffer};

  const char* part1 = "abcd";
  buffer.feed(part1, strlen(part1));

  ASSERT_EQ(cursor.current(), 'a');

  ASSERT_TRUE(cursor.advance(1));
  ASSERT_EQ(cursor.current(), 'b');

  ASSERT_TRUE(cursor.advance(0));
  ASSERT_EQ(cursor.current(), 'b');

  ASSERT_TRUE(cursor.advance(1));
  ASSERT_EQ(cursor.current(), 'c');

  const char* part2 = "efgh";
  buffer.feed(part2, strlen(part2));

  ASSERT_TRUE(cursor.advance(2));
  ASSERT_EQ(cursor.current(), 'e');

  ASSERT_FALSE(cursor.advance(5));
}

TEST(stream, test_cursor_remaining_for_array) {
  ArrayStreamBuf<char> buffer;
  StreamCursor cursor{&buffer};

  const char* data = "abcd";
  buffer.feed(data, strlen(data));
  ASSERT_EQ(cursor.remaining(), 4u);

  cursor.advance(2);
  ASSERT_EQ(cursor.remaining(), 2u);

  cursor.advance(1);
  ASSERT_EQ(cursor.remaining(), 1u);

  cursor.advance(1);
  ASSERT_EQ(cursor.remaining(), 0u);
}

TEST(stream, test_cursor_eol_eof_for_array) {
  ArrayStreamBuf<char> buffer;
  StreamCursor cursor{&buffer};

  const char* data = "abcd\r\nefgh";
  buffer.feed(data, strlen(data));

  cursor.advance(4);
  ASSERT_TRUE(cursor.eol());
  ASSERT_FALSE(cursor.eof());

  cursor.advance(2);
  ASSERT_FALSE(cursor.eol());
  ASSERT_FALSE(cursor.eof());

  cursor.advance(4);
  ASSERT_FALSE(cursor.eol());
  ASSERT_TRUE(cursor.eof());
}

TEST(stream, test_cursor_offset_for_array) {
  ArrayStreamBuf<char> buffer;
  StreamCursor cursor{&buffer};

  const char* data = "abcdefgh";
  buffer.feed(data, strlen(data));

  size_t shift = 4u;
  cursor.advance(shift);

  std::string result{cursor.offset(), strlen(data) - shift};
  ASSERT_EQ(result, "efgh");
}

TEST(stream, test_cursor_diff_for_array) {
  ArrayStreamBuf<char> buffer1;
  StreamCursor first_cursor{&buffer1};
  ArrayStreamBuf<char> buffer2;
  StreamCursor second_cursor{&buffer2};

  const char* data = "abcdefgh";
  buffer1.feed(data, strlen(data));
  buffer2.feed(data, strlen(data));

  ASSERT_EQ(first_cursor.diff(second_cursor), 0u);
  ASSERT_EQ(second_cursor.diff(first_cursor), 0u);

  first_cursor.advance(4);
  ASSERT_EQ(second_cursor.diff(first_cursor), 4u);

  second_cursor.advance(4);
  ASSERT_EQ(second_cursor.diff(first_cursor), 0u);
}
