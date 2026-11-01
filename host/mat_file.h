// MAT-file writer on matio: level 5 with zlib compression, as MATLAB's default save, for the
// matrices the dataset layout of export_dataset.m needs (double, int16, uint8, complex double, text).
// Read by MATLAB, scipy.io.loadmat and nn_training_model.
#pragma once

#include <matio.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace arof {

template <class T> struct MatType;
template <> struct MatType<double>   { static constexpr matio_classes cls = MAT_C_DOUBLE; static constexpr matio_types type = MAT_T_DOUBLE; };
template <> struct MatType<int16_t>  { static constexpr matio_classes cls = MAT_C_INT16;  static constexpr matio_types type = MAT_T_INT16; };
template <> struct MatType<uint8_t>  { static constexpr matio_classes cls = MAT_C_UINT8;  static constexpr matio_types type = MAT_T_UINT8; };
template <> struct MatType<uint16_t> { static constexpr matio_classes cls = MAT_C_CHAR;   static constexpr matio_types type = MAT_T_UINT16; };

class MatFile {
public:
    explicit MatFile(const std::string& path) : mat_(Mat_CreateVer(path.c_str(), nullptr, MAT_FT_MAT5)) {
        if (!mat_) throw std::runtime_error("cannot write " + path);
    }
    ~MatFile() { Mat_Close(mat_); }
    MatFile(const MatFile&) = delete;
    MatFile& operator=(const MatFile&) = delete;

    // rows x cols, column-major; im = nullptr for a real matrix
    template <class T>
    void add(const std::string& name, const T* re, const T* im, size_t rows, size_t cols) {
        size_t dims[2] = {rows, cols};
        mat_complex_split_t z{const_cast<T*>(re), const_cast<T*>(im)};
        void* data = im ? static_cast<void*>(&z) : const_cast<T*>(re);
        const int opt = MAT_F_DONT_COPY_DATA | (im ? MAT_F_COMPLEX : 0);
        matvar_t* v = Mat_VarCreate(name.c_str(), MatType<T>::cls, MatType<T>::type, 2, dims, data, opt);
        if (!v || Mat_VarWrite(mat_, v, MAT_COMPRESSION_ZLIB) != 0) {
            if (v) Mat_VarFree(v);
            throw std::runtime_error("cannot write variable " + name);
        }
        Mat_VarFree(v);                             // DONT_COPY_DATA: the data stays ours
    }

    template <class T>
    void scalar(const std::string& name, T v) { add<T>(name, &v, nullptr, 1, 1); }

    void text(const std::string& name, const std::string& s) {   // 1 x n char (ASCII)
        const std::vector<uint16_t> c(s.begin(), s.end());
        add<uint16_t>(name, c.data(), nullptr, 1, c.size());
    }

private:
    mat_t* mat_;
};

}  // namespace arof
