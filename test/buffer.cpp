#include "utils/string.h"
#include "test-utils.hpp"
#include "mock-io.hpp"
#include "core/buffer.hpp"
#include "core/fdutil.hpp"

using namespace cerb;

typedef BufferTestBase BufferTest;

TEST_F(BufferTest, AsString)
{
    Buffer buffer("need a light");
    ASSERT_EQ(12, buffer.size());
    ASSERT_EQ(std::string("need a light"), buffer.to_string());

    Buffer cuffer("ghost reporting");
    ASSERT_EQ(15, cuffer.size());
    ASSERT_EQ(std::string("ghost reporting"), cuffer.to_string());

    buffer.append_from(cuffer.begin(), cuffer.end());
    ASSERT_EQ(27, buffer.size());
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
    std::shared_ptr<Buffer> head(new Buffer("0123456789abcdefghij"));
    std::shared_ptr<Buffer> body(new Buffer("QWEASDZXC+RTYFGHVBN-"));
    std::shared_ptr<Buffer> tail(new Buffer("!@#$%^&*()ABCDEFGHIJ"));

    {
        BufferTest::io_obj->clear();
        BufferSet bufset;
        bufset.append(head);
        bufset.append(body);
        bufset.append(tail);

        bool w = bufset.writev(0);
        ASSERT_TRUE(w);
        ASSERT_TRUE(bufset.empty());

        ASSERT_EQ(3, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head->to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ(body->to_string(), BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ(tail->to_string(), BufferTest::io_obj->write_buffer[2]);
    }

    {
        BufferTest::io_obj->clear();
        BufferTest::io_obj->writing_sizes.push_back(50);

        BufferSet bufset;
        bufset.append(head);
        bufset.append(body);
        bufset.append(tail);

        bool w = bufset.writev(0);
        ASSERT_FALSE(w);
        ASSERT_FALSE(bufset.empty());

        ASSERT_EQ(3, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head->to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ(body->to_string(), BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ("!@#$%^&*()", BufferTest::io_obj->write_buffer[2]);

        w = bufset.writev(0);
        ASSERT_TRUE(w);
        ASSERT_TRUE(bufset.empty());

        ASSERT_EQ(4, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head->to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ(body->to_string(), BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ("!@#$%^&*()", BufferTest::io_obj->write_buffer[2]);
        ASSERT_EQ("ABCDEFGHIJ", BufferTest::io_obj->write_buffer[3]);
    }

    {
        BufferTest::io_obj->clear();
        BufferTest::io_obj->writing_sizes.push_back(30);
        BufferTest::io_obj->writing_sizes.push_back(17);

        BufferSet bufset;
        bufset.append(head);
        bufset.append(body);
        bufset.append(tail);

        bool w = bufset.writev(0);
        ASSERT_FALSE(w);
        ASSERT_FALSE(bufset.empty());

        ASSERT_EQ(2, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head->to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);

        w = bufset.writev(0);
        ASSERT_FALSE(w);
        ASSERT_FALSE(bufset.empty());

        ASSERT_EQ(4, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head->to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ("RTYFGHVBN-", BufferTest::io_obj->write_buffer[2]);
        ASSERT_EQ("!@#$%^&", BufferTest::io_obj->write_buffer[3]);

        w = bufset.writev(0);
        ASSERT_TRUE(w);
        ASSERT_TRUE(bufset.empty());

        ASSERT_EQ(5, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head->to_string(), BufferTest::io_obj->write_buffer[0]);
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
        bufset.append(head);
        bufset.append(body);
        bufset.append(tail);

        bool w = bufset.writev(0);
        ASSERT_FALSE(w);
        ASSERT_FALSE(bufset.empty());

        ASSERT_EQ(2, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head->to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);

        w = bufset.writev(0);
        ASSERT_FALSE(w);
        ASSERT_FALSE(bufset.empty());

        ASSERT_EQ(4, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head->to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ("RTYFGHVBN-", BufferTest::io_obj->write_buffer[2]);
        ASSERT_EQ("!@#$%^&*()", BufferTest::io_obj->write_buffer[3]);

        w = bufset.writev(0);
        ASSERT_TRUE(w);
        ASSERT_TRUE(bufset.empty());

        ASSERT_EQ(5, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head->to_string(), BufferTest::io_obj->write_buffer[0]);
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
        bufset.append(head);
        bufset.append(body);
        bufset.append(tail);

        bool w = bufset.writev(0);
        ASSERT_FALSE(w);
        ASSERT_FALSE(bufset.empty());

        ASSERT_EQ(2, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head->to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);

        w = bufset.writev(0);
        ASSERT_FALSE(w);
        ASSERT_FALSE(bufset.empty());

        ASSERT_EQ(4, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head->to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ("RTYFGHVBN-", BufferTest::io_obj->write_buffer[2]);
        ASSERT_EQ("!@#$%^&*()ABC", BufferTest::io_obj->write_buffer[3]);

        w = bufset.writev(0);
        ASSERT_TRUE(w);
        ASSERT_TRUE(bufset.empty());

        ASSERT_EQ(5, BufferTest::io_obj->write_buffer.size());
        ASSERT_EQ(head->to_string(), BufferTest::io_obj->write_buffer[0]);
        ASSERT_EQ("QWEASDZXC+", BufferTest::io_obj->write_buffer[1]);
        ASSERT_EQ("RTYFGHVBN-", BufferTest::io_obj->write_buffer[2]);
        ASSERT_EQ("!@#$%^&*()ABC", BufferTest::io_obj->write_buffer[3]);
        ASSERT_EQ("DEFGHIJ", BufferTest::io_obj->write_buffer[4]);
    }
}

TEST_F(BufferTest, PartiallyWrite)
{
    std::shared_ptr<Buffer> head(new Buffer("0123456789!@#$%^&*()"));
    std::shared_ptr<Buffer> body(new Buffer("QWERTYUIOPqwertyuiop"));
    std::shared_ptr<Buffer> tail(new Buffer("ABCDEFGHIJabcdefghij"));

    // exactly 20 bytes

    BufferTest::io_obj->writing_sizes.push_back(20);
    BufferTest::io_obj->writing_sizes.push_back(-1);
    BufferTest::io_obj->writing_sizes.push_back(20);
    BufferTest::io_obj->writing_sizes.push_back(-1);
    BufferTest::io_obj->writing_sizes.push_back(20);
    BufferTest::io_obj->writing_sizes.push_back(-1);

    BufferSet buf;
    buf.append(head);
    buf.append(body);
    buf.append(tail);

    bool w = buf.writev(0);
    ASSERT_FALSE(w);
    ASSERT_EQ(1, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ(head->to_string(), BufferTest::io_obj->write_buffer[0]);
    ASSERT_FALSE(buf.empty());
    BufferTest::io_obj->write_buffer.clear();

    w = buf.writev(0);
    ASSERT_FALSE(w);
    ASSERT_EQ(1, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ(body->to_string(), BufferTest::io_obj->write_buffer[0]);
    ASSERT_FALSE(buf.empty());
    BufferTest::io_obj->write_buffer.clear();

    w = buf.writev(0);
    ASSERT_TRUE(w);
    ASSERT_EQ(1, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ(tail->to_string(), BufferTest::io_obj->write_buffer[0]);
    ASSERT_TRUE(buf.empty());
    BufferTest::io_obj->write_buffer.clear();
    BufferTest::io_obj->writing_sizes.clear();

    // 1 byte less

    BufferTest::io_obj->writing_sizes.push_back(19);
    BufferTest::io_obj->writing_sizes.push_back(19);
    BufferTest::io_obj->writing_sizes.push_back(19);
    BufferTest::io_obj->writing_sizes.push_back(19);

    buf.append(head);
    buf.append(body);
    buf.append(tail);

    w = buf.writev(0);
    ASSERT_FALSE(w);
    ASSERT_EQ(1, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ("0123456789!@#$%^&*(", BufferTest::io_obj->write_buffer[0]);
    ASSERT_FALSE(buf.empty());
    BufferTest::io_obj->write_buffer.clear();

    w = buf.writev(0);
    ASSERT_FALSE(w);
    ASSERT_EQ(2, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ(")", BufferTest::io_obj->write_buffer[0]);
    ASSERT_EQ("QWERTYUIOPqwertyui", BufferTest::io_obj->write_buffer[1]);
    ASSERT_FALSE(buf.empty());
    BufferTest::io_obj->write_buffer.clear();

    w = buf.writev(0);
    ASSERT_FALSE(w);
    ASSERT_EQ(2, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ("op", BufferTest::io_obj->write_buffer[0]);
    ASSERT_EQ("ABCDEFGHIJabcdefg", BufferTest::io_obj->write_buffer[1]);
    ASSERT_FALSE(buf.empty());
    BufferTest::io_obj->write_buffer.clear();

    w = buf.writev(0);
    ASSERT_TRUE(w);
    ASSERT_EQ(1, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ("hij", BufferTest::io_obj->write_buffer[0]);
    ASSERT_TRUE(buf.empty());
    BufferTest::io_obj->write_buffer.clear();
    BufferTest::io_obj->writing_sizes.clear();

    // 1 byte more

    BufferTest::io_obj->writing_sizes.push_back(21);
    BufferTest::io_obj->writing_sizes.push_back(21);
    BufferTest::io_obj->writing_sizes.push_back(21);

    buf.append(head);
    buf.append(body);
    buf.append(tail);

    w = buf.writev(0);
    ASSERT_FALSE(w);
    ASSERT_EQ(2, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ("0123456789!@#$%^&*()", BufferTest::io_obj->write_buffer[0]);
    ASSERT_EQ("Q", BufferTest::io_obj->write_buffer[1]);
    ASSERT_FALSE(buf.empty());
    BufferTest::io_obj->write_buffer.clear();

    w = buf.writev(0);
    ASSERT_FALSE(w);
    ASSERT_EQ(2, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ("WERTYUIOPqwertyuiop", BufferTest::io_obj->write_buffer[0]);
    ASSERT_EQ("AB", BufferTest::io_obj->write_buffer[1]);
    ASSERT_FALSE(buf.empty());
    BufferTest::io_obj->write_buffer.clear();

    w = buf.writev(0);
    ASSERT_TRUE(w);
    ASSERT_EQ(1, BufferTest::io_obj->write_buffer.size());
    ASSERT_EQ("CDEFGHIJabcdefghij", BufferTest::io_obj->write_buffer[0]);
    ASSERT_TRUE(buf.empty());
    BufferTest::io_obj->write_buffer.clear();
    BufferTest::io_obj->writing_sizes.clear();
}

TEST_F(BufferTest, WriteSinglePieceMultipleTimes)
{
    std::shared_ptr<Buffer> x(new Buffer("0123456789abcdefghij"));
    std::shared_ptr<Buffer> y(new Buffer("QWEASDZXC+RTYFGHVBN-"));

    BufferTest::io_obj->writing_sizes.push_back(1);
    BufferTest::io_obj->writing_sizes.push_back(2);
    BufferTest::io_obj->writing_sizes.push_back(3);
    BufferTest::io_obj->writing_sizes.push_back(4);
    BufferTest::io_obj->writing_sizes.push_back(5);
    BufferTest::io_obj->writing_sizes.push_back(6);
    BufferTest::io_obj->writing_sizes.push_back(2);

    BufferSet bufset;
    bufset.append(x);
    bufset.append(y);

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
    int const WRITEV_SIZE = 2 * 1024 * 1024;
    int fill_x = WRITEV_SIZE * 3 / 4;
    int fill_y = WRITEV_SIZE * 4 / 5;
    int fill_z = WRITEV_SIZE * 4 / 3;

    std::shared_ptr<Buffer> b(new Buffer("begin"));
    std::shared_ptr<Buffer> x(new Buffer(std::string(fill_x, 'x')));
    std::shared_ptr<Buffer> y(new Buffer(std::string(fill_y, 'y')));
    std::shared_ptr<Buffer> z(new Buffer(std::string(fill_z, 'z')));
    std::shared_ptr<Buffer> a(new Buffer("abc"));
    std::shared_ptr<Buffer> m(new Buffer(std::string(WRITEV_SIZE, 'm')));
    std::shared_ptr<Buffer> u(new Buffer("uvw"));

    BufferSet bufset;
    bufset.append(b);
    bufset.append(x);
    bufset.append(y);
    bufset.append(z);
    bufset.append(a);
    bufset.append(m);
    bufset.append(u);

    bool w = bufset.writev(0);
    ASSERT_TRUE(w);
    ASSERT_TRUE(bufset.empty());

    ASSERT_EQ(7, BufferTest::io_obj->write_buffer.size());

    ASSERT_EQ(b->size(), BufferTest::io_obj->write_buffer[0].size());
    ASSERT_EQ("begin", BufferTest::io_obj->write_buffer[0]);

    ASSERT_EQ(fill_x, BufferTest::io_obj->write_buffer[1].size());
    for (unsigned i = 0; i < BufferTest::io_obj->write_buffer[1].size(); ++i) {
        ASSERT_EQ('x', BufferTest::io_obj->write_buffer[1][i]) << " at " << i;
    }

    ASSERT_EQ(fill_y, BufferTest::io_obj->write_buffer[2].size());
    for (unsigned i = b->size(); i < BufferTest::io_obj->write_buffer[2].size(); ++i) {
        ASSERT_EQ('y', BufferTest::io_obj->write_buffer[2][i]) << " at " << i;
    }

    ASSERT_EQ(fill_z, BufferTest::io_obj->write_buffer[3].size());
    for (unsigned i = b->size(); i < BufferTest::io_obj->write_buffer[3].size(); ++i) {
        ASSERT_EQ('z', BufferTest::io_obj->write_buffer[3][i]) << " at " << i;
    }

    ASSERT_EQ(a->size(), BufferTest::io_obj->write_buffer[4].size());
    ASSERT_EQ("abc", BufferTest::io_obj->write_buffer[4]);

    ASSERT_EQ(WRITEV_SIZE, BufferTest::io_obj->write_buffer[5].size());
    for (unsigned i = b->size(); i < BufferTest::io_obj->write_buffer[5].size(); ++i) {
        ASSERT_EQ('m', BufferTest::io_obj->write_buffer[5][i]) << " at " << i;
    }

    ASSERT_EQ(u->size(), BufferTest::io_obj->write_buffer[6].size());
    ASSERT_EQ("uvw", BufferTest::io_obj->write_buffer[6]);
}

TEST_F(BufferTest, Write50KBuffers)
{
    ASSERT_EQ(10, (std::string("ab") * 5).size());
    int const SIZE = 50000;
    std::vector<std::shared_ptr<Buffer>> storage;
    BufferSet bufset;
    for (int i = 0; i < SIZE; ++i) {
        storage.push_back(std::shared_ptr<Buffer>(new Buffer(
            ("VALUE:" + util::str(100000000LL + i) + '$') * 40)));
    }
    for (int i = 0; i < SIZE; ++i) {
        bufset.append(storage[i]);
    }

    bool w = bufset.writev(0);
    ASSERT_TRUE(w);

    ASSERT_EQ(SIZE, BufferTest::io_obj->write_buffer.size());
    for (int i = 0; i < SIZE; ++i) {
        ASSERT_EQ(("VALUE:" + util::str(100000000LL + i) + '$') * 40,
                  BufferTest::io_obj->write_buffer[i])
            << " at " << i;
    }
}
