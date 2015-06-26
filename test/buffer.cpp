#include "utils/string.h"
#include "mock-io.hpp"
#include "core/buffer.hpp"
#include "core/fdutil.hpp"

using namespace cerb;

typedef BufferTestBase BufferTest;

TEST_F(BufferTest, AsString)
{
    Buffer buffer(Buffer::from_string("need a light"));
    Buffer cuffer(Buffer::from_string("ghost reporting"));
    buffer.append_from(cuffer.begin(), cuffer.end());
    ASSERT_EQ(std::string("need a lightghost reporting"), buffer.to_string());
}

TEST_F(BufferTest, IO)
{
    Buffer buffer;
    int n = buffer.read(-1);
    ASSERT_EQ(0, n);
    ASSERT_TRUE(buffer.empty());

    BufferTest::io_obj->read_buffer.push_back("the quick brown fox jumps over");
    BufferTest::io_obj->read_buffer.push_back(" a lazy dog");
    int buf_len = BufferTest::io_obj->read_buffer[0].size()
                + BufferTest::io_obj->read_buffer[1].size();

    n = buffer.read(-1);
    ASSERT_EQ(buf_len, n);
    ASSERT_EQ(std::string("the quick brown fox jumps over a lazy dog"), buffer.to_string());
}

TEST_F(BufferTest, WriteVectorSimple)
{
    Buffer head(Buffer::from_string("0123456789abcdefghij"));
    Buffer body(Buffer::from_string("QWEASDZXC+RTYFGHVBN-"));
    Buffer tail(Buffer::from_string("!@#$%^&*()ABCDEFGHIJ"));

    {
        BufferTest::io_obj->clear();
        BufferSet bufset;
        bufset.append(util::mkref(head));
        bufset.append(util::mkref(body));
        bufset.append(util::mkref(tail));

        bool w = bufset.writev(0);
        ASSERT_TRUE(w);
        ASSERT_TRUE(bufset.empty());

        ASSERT_EQ(3, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head.to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ(body.to_string(), BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ(tail.to_string(), BufferTest::io_obj->write_buffer[2]);
    }

    {
        BufferTest::io_obj->clear();
        BufferTest::io_obj->writing_sizes.push_back(50);

        BufferSet bufset;
        bufset.append(util::mkref(head));
        bufset.append(util::mkref(body));
        bufset.append(util::mkref(tail));

        bool w = bufset.writev(0);
        ASSERT_FALSE(w);
        ASSERT_FALSE(bufset.empty());

        ASSERT_EQ(3, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head.to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ(body.to_string(), BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ("!@#$%^&*()", BufferTest::io_obj->write_buffer[2]);

        w = bufset.writev(0);
        ASSERT_TRUE(w);
        ASSERT_TRUE(bufset.empty());

        ASSERT_EQ(4, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head.to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ(body.to_string(), BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ("!@#$%^&*()", BufferTest::io_obj->write_buffer[2]);
        ASSERT_EQ("ABCDEFGHIJ", BufferTest::io_obj->write_buffer[3]);
    }

    {
        BufferTest::io_obj->clear();
        BufferTest::io_obj->writing_sizes.push_back(30);
        BufferTest::io_obj->writing_sizes.push_back(17);

        BufferSet bufset;
        bufset.append(util::mkref(head));
        bufset.append(util::mkref(body));
        bufset.append(util::mkref(tail));

        bool w = bufset.writev(0);
        ASSERT_FALSE(w);
        ASSERT_FALSE(bufset.empty());

        ASSERT_EQ(2, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head.to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);

        w = bufset.writev(0);
        ASSERT_FALSE(w);
        ASSERT_FALSE(bufset.empty());

        ASSERT_EQ(4, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head.to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ("RTYFGHVBN-", BufferTest::io_obj->write_buffer[2]);
        ASSERT_EQ("!@#$%^&", BufferTest::io_obj->write_buffer[3]);

        w = bufset.writev(0);
        ASSERT_TRUE(w);
        ASSERT_TRUE(bufset.empty());

        ASSERT_EQ(5, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head.to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ("RTYFGHVBN-", BufferTest::io_obj->write_buffer[2]);
        ASSERT_EQ("!@#$%^&", BufferTest::io_obj->write_buffer[3]);
        ASSERT_EQ("*()ABCDEFGHIJ", BufferTest::io_obj->write_buffer[4]);
    }

    {
        BufferTest::io_obj->clear();
        BufferTest::io_obj->writing_sizes.push_back(30);
        BufferTest::io_obj->writing_sizes.push_back(20);

        BufferSet bufset;
        bufset.append(util::mkref(head));
        bufset.append(util::mkref(body));
        bufset.append(util::mkref(tail));

        bool w = bufset.writev(0);
        ASSERT_FALSE(w);
        ASSERT_FALSE(bufset.empty());

        ASSERT_EQ(2, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head.to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);

        w = bufset.writev(0);
        ASSERT_FALSE(w);
        ASSERT_FALSE(bufset.empty());

        ASSERT_EQ(4, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head.to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ("RTYFGHVBN-", BufferTest::io_obj->write_buffer[2]);
        ASSERT_EQ("!@#$%^&*()", BufferTest::io_obj->write_buffer[3]);

        w = bufset.writev(0);
        ASSERT_TRUE(w);
        ASSERT_TRUE(bufset.empty());

        ASSERT_EQ(5, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head.to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ("RTYFGHVBN-", BufferTest::io_obj->write_buffer[2]);
        ASSERT_EQ("!@#$%^&*()", BufferTest::io_obj->write_buffer[3]);
        ASSERT_EQ("ABCDEFGHIJ", BufferTest::io_obj->write_buffer[4]);
    }

    {
        BufferTest::io_obj->clear();
        BufferTest::io_obj->writing_sizes.push_back(30);
        BufferTest::io_obj->writing_sizes.push_back(23);

        BufferSet bufset;
        bufset.append(util::mkref(head));
        bufset.append(util::mkref(body));
        bufset.append(util::mkref(tail));

        bool w = bufset.writev(0);
        ASSERT_FALSE(w);
        ASSERT_FALSE(bufset.empty());

        ASSERT_EQ(2, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head.to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);

        w = bufset.writev(0);
        ASSERT_FALSE(w);
        ASSERT_FALSE(bufset.empty());

        ASSERT_EQ(4, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head.to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ("RTYFGHVBN-", BufferTest::io_obj->write_buffer[2]);
        ASSERT_EQ("!@#$%^&*()ABC", BufferTest::io_obj->write_buffer[3]);

        w = bufset.writev(0);
        ASSERT_TRUE(w);
        ASSERT_TRUE(bufset.empty());

        ASSERT_EQ(5, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head.to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ("RTYFGHVBN-", BufferTest::io_obj->write_buffer[2]);
        ASSERT_EQ("!@#$%^&*()ABC", BufferTest::io_obj->write_buffer[3]);
        ASSERT_EQ("DEFGHIJ", BufferTest::io_obj->write_buffer[4]);
    }
}

TEST_F(BufferTest, WriteSinglePieceMultipleTimes)
{
    Buffer x(Buffer::from_string("0123456789abcdefghij"));
    Buffer y(Buffer::from_string("QWEASDZXC+RTYFGHVBN-"));

    BufferTest::io_obj->writing_sizes.push_back(1);
    BufferTest::io_obj->writing_sizes.push_back(2);
    BufferTest::io_obj->writing_sizes.push_back(3);
    BufferTest::io_obj->writing_sizes.push_back(4);
    BufferTest::io_obj->writing_sizes.push_back(5);
    BufferTest::io_obj->writing_sizes.push_back(6);
    BufferTest::io_obj->writing_sizes.push_back(2);

    BufferSet bufset;
    bufset.append(util::mkref(x));
    bufset.append(util::mkref(y));

    bool w = bufset.writev(0);
    ASSERT_FALSE(w);
    ASSERT_FALSE(bufset.empty());

    ASSERT_EQ(1, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ("0", BufferTest::io_obj->write_buffer[0]);

    w = bufset.writev(0);
    ASSERT_FALSE(w);
    ASSERT_FALSE(bufset.empty());

    ASSERT_EQ(2, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ("0", BufferTest::io_obj->write_buffer[0]);
    ASSERT_EQ("12", BufferTest::io_obj->write_buffer[1]);

    w = bufset.writev(0);
    ASSERT_FALSE(w);
    ASSERT_FALSE(bufset.empty());

    ASSERT_EQ(3, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ("12", BufferTest::io_obj->write_buffer[1]);
    ASSERT_EQ("345", BufferTest::io_obj->write_buffer[2]);

    w = bufset.writev(0);
    ASSERT_FALSE(w);
    ASSERT_FALSE(bufset.empty());

    ASSERT_EQ(4, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ("345", BufferTest::io_obj->write_buffer[2]);
    ASSERT_EQ("6789", BufferTest::io_obj->write_buffer[3]);

    w = bufset.writev(0);
    ASSERT_FALSE(w);
    ASSERT_FALSE(bufset.empty());

    ASSERT_EQ(5, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ("6789", BufferTest::io_obj->write_buffer[3]);
    ASSERT_EQ("abcde", BufferTest::io_obj->write_buffer[4]);

    w = bufset.writev(0);
    ASSERT_FALSE(w);
    ASSERT_FALSE(bufset.empty());

    ASSERT_EQ(7, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ("abcde", BufferTest::io_obj->write_buffer[4]);
    ASSERT_EQ("fghij", BufferTest::io_obj->write_buffer[5]);
    ASSERT_EQ("Q", BufferTest::io_obj->write_buffer[6]);

    w = bufset.writev(0);
    ASSERT_FALSE(w);
    ASSERT_FALSE(bufset.empty());

    ASSERT_EQ(8, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ("fghij", BufferTest::io_obj->write_buffer[5]);
    ASSERT_EQ("Q", BufferTest::io_obj->write_buffer[6]);
    ASSERT_EQ("WE", BufferTest::io_obj->write_buffer[7]);

    w = bufset.writev(0);
    ASSERT_TRUE(w);
    ASSERT_TRUE(bufset.empty());

    ASSERT_EQ(9, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ("ASDZXC+RTYFGHVBN-", BufferTest::io_obj->write_buffer[8]);
}

TEST_F(BufferTest, WriteVectorLargeBuffer)
{
    int const WRITEV_MAX_SIZE = 2 * 1024 * 1024;
    int fill_x = WRITEV_MAX_SIZE * 3 / 4;
    int fill_y = WRITEV_MAX_SIZE * 4 / 5;
    int fill_z = WRITEV_MAX_SIZE * 4 / 3;

    Buffer b(Buffer::from_string("begin"));
    Buffer x(Buffer::from_string(std::string(fill_x, 'x')));
    Buffer y(Buffer::from_string(std::string(fill_y, 'y')));
    Buffer z(Buffer::from_string(std::string(fill_z, 'z')));
    Buffer a(Buffer::from_string("abc"));
    Buffer m(Buffer::from_string(std::string(WRITEV_MAX_SIZE, 'm')));
    Buffer u(Buffer::from_string("uvw"));

    BufferSet bufset;
    bufset.append(util::mkref(b));
    bufset.append(util::mkref(x));
    bufset.append(util::mkref(y));
    bufset.append(util::mkref(z));
    bufset.append(util::mkref(a));
    bufset.append(util::mkref(m));
    bufset.append(util::mkref(u));

    bool w = bufset.writev(0);
    ASSERT_TRUE(w);
    ASSERT_TRUE(bufset.empty());

    ASSERT_EQ(7, BufferTest::io_obj->write_buffer.size());

    ASSERT_EQ(b.size(), BufferTest::io_obj->write_buffer[0].size());
    ASSERT_EQ("begin", BufferTest::io_obj->write_buffer[0]);

    ASSERT_EQ(fill_x, BufferTest::io_obj->write_buffer[1].size());
    for (unsigned i = 0; i < BufferTest::io_obj->write_buffer[1].size(); ++i) {
        ASSERT_EQ('x', BufferTest::io_obj->write_buffer[1][i]) << " at " << i;
    }

    ASSERT_EQ(fill_y, BufferTest::io_obj->write_buffer[2].size());
    for (unsigned i = b.size(); i < BufferTest::io_obj->write_buffer[2].size(); ++i) {
        ASSERT_EQ('y', BufferTest::io_obj->write_buffer[2][i]) << " at " << i;
    }

    ASSERT_EQ(fill_z, BufferTest::io_obj->write_buffer[3].size());
    for (unsigned i = b.size(); i < BufferTest::io_obj->write_buffer[3].size(); ++i) {
        ASSERT_EQ('z', BufferTest::io_obj->write_buffer[3][i]) << " at " << i;
    }

    ASSERT_EQ(a.size(), BufferTest::io_obj->write_buffer[4].size());
    ASSERT_EQ("abc", BufferTest::io_obj->write_buffer[4]);

    ASSERT_EQ(WRITEV_MAX_SIZE, BufferTest::io_obj->write_buffer[5].size());
    for (unsigned i = b.size(); i < BufferTest::io_obj->write_buffer[5].size(); ++i) {
        ASSERT_EQ('m', BufferTest::io_obj->write_buffer[5][i]) << " at " << i;
    }

    ASSERT_EQ(u.size(), BufferTest::io_obj->write_buffer[6].size());
    ASSERT_EQ("uvw", BufferTest::io_obj->write_buffer[6]);
}

TEST_F(BufferTest, Write50KBuffers)
{
    int const SIZE = 500000;
    std::vector<Buffer> storage;
    BufferSet bufset;
    for (int i = 0; i < SIZE; ++i) {
        storage.push_back(Buffer::from_string("VALUE:" + util::str(100000000LL + i) + '$'));
    }
    for (int i = 0; i < SIZE; ++i) {
        bufset.append(util::mkref(storage[i]));
    }

    bool w = bufset.writev(0);
    ASSERT_TRUE(w);

    ASSERT_EQ(SIZE, BufferTest::io_obj->write_buffer.size());
    for (int i = 0; i < SIZE; ++i) {
        ASSERT_EQ("VALUE:" + util::str(100000000LL + i) + '$',
                  BufferTest::io_obj->write_buffer[i])
            << " at " << i;
    }
}
