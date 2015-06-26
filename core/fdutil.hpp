#ifndef __CERBERUS_FILE_DESCRIPTER_UTILITY_HPP__
#define __CERBERUS_FILE_DESCRIPTER_UTILITY_HPP__

namespace cerb {

    class FDWrapper {
    public:
        int fd;

        FDWrapper(int fd)
            : fd(fd)
        {}

        FDWrapper(FDWrapper const&) = delete;

        ~FDWrapper();

        bool closed() const;
        void close();
    };

}

#endif /* __CERBERUS_FILE_DESCRIPTER_UTILITY_HPP__ */
