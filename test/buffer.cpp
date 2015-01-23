#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fstream>
#include <gtest/gtest.h>

#include "core/buffer.hpp"
#include "core/fdutil.hpp"

using namespace cerb;

static Buffer::size_type fsize(char const* filename)
{
    struct stat st; 
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return -1; 
}

static bool filecmp(char const* fn, char const* gn)
{
    std::ifstream f(fn, std::ifstream::in | std::ifstream::binary);
    std::ifstream g(gn, std::ifstream::in | std::ifstream::binary);

    EXPECT_TRUE(f.good());
    EXPECT_TRUE(g.good());
    if (!(f.good() && g.good())) {
        return false;
    }
    int const size_len = 4096;
    char fbuf[size_len], gbuf[size_len];
    while (true) {
        auto fsize = f.read(fbuf, size_len).gcount();
        auto gsize = g.read(gbuf, size_len).gcount();
        if (fsize != gsize) {
            return false;
        }
        if (memcmp(fbuf, gbuf, fsize)) {
            return false;
        }
        if (f.eof()) {
            return true;
        }
    }
}

TEST(Buffer, AsString)
{
    Buffer buffer(Buffer::from_string("need a light"));
    Buffer cuffer(Buffer::from_string("ghost reporting"));
    buffer.append_from(cuffer.begin(), cuffer.end());
    ASSERT_EQ(std::string("need a lightghost reporting"), buffer.to_string());
}

TEST(Buffer, IO)
{
    char const input[] = "test/asset/read.txt";
    FDWrapper r(::open(input, O_RDONLY));
    int rfd = r.fd;
    ASSERT_NE(-1, rfd);
    Buffer::size_type filesize = fsize(input);
    ASSERT_NE(-1, filesize);

    Buffer buffer;
    buffer.read(rfd);
    ASSERT_EQ(filesize, buffer.size());
    ASSERT_EQ('<', *buffer.begin());
    ASSERT_EQ('!', *++buffer.begin());

    char const output[] = "tmp.test-buffer-write.txt";
    FDWrapper w(::open(output, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR));
    int wfd = w.fd;
    ASSERT_NE(-1, wfd);
    buffer.write(wfd);
    ASSERT_TRUE(filecmp(input, output));
}
