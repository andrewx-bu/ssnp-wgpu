#include "ssnp_model.h"
#include <cmath>
#include <vector>
#include <complex>
#include <cassert>

std::vector<float> scatter_factor(const std::vector<float>& n, float res_z, float dz, float n0) {
    float factor = std::pow(2 * M_PI * res_z / n0, 2) * dz;
    std::vector<float> result(n.size());
    for (size_t i = 0; i < n.size(); ++i) {
        result[i] = factor * n[i] * (2 * n0 + n[i]);
    }
    return result;
}

std::vector<float> near_0(int size) {
    std::vector<float> result(size);
    for (int i = 0; i < size; ++i) {
        result[i] = std::fmod((i / float(size)) + 0.5f, 1.0f) - 0.5f;
    }
    return result;
}

std::vector<std::vector<std::complex<float>>> c_gamma(const std::vector<float>& res, const std::vector<int>& shape) {
    // Generate c_beta and c_alpha
    int size_alpha = shape[0];
    int size_beta = shape[1];
    std::vector<float> c_alpha = near_0(size_alpha);
    std::vector<float> c_beta = near_0(size_beta);

    // Normalize by resolution
    for (int i = 0; i < size_alpha; ++i) {
        c_alpha[i] /= res[1];
    }

    for (int i = 0; i < size_beta; ++i) {
        c_beta[i] /= res[0];
    }

    std::vector<std::vector<std::complex<float>>> result(1, std::vector<std::complex<float>>(size_alpha * size_beta));
    float eps = 1E-8f; // epsilon to avoid sqrt of negative numbers
    for (int i = 0; i < size_alpha; ++i) {
        for (int j = 0; j < size_beta; ++j) {
            float val = 1.0f - (std::pow(c_alpha[i], 2) + std::pow(c_beta[j], 2));
            result[0][i * size_beta + j] = std::sqrt(std::max(val, eps));
        }
    }
    // Unsqueeze
    return {result};
}

std::pair<std::vector<std::vector<std::complex<double>>>, std::vector<std::vector<std::complex<double>>>>
diffract(const std::vector<std::vector<std::complex<double>>>& uf,
         const std::vector<std::vector<std::complex<double>>>& ub,
         const std::vector<float>& res, 
         float dz) {
    assert(uf.size() == ub.size() && uf[0].size() == ub[0].size());

    int batch_size = uf.size();
    int size = uf[0].size();  // Flattened total size

    // Calculate cgamma
    auto shape = std::vector<int>{static_cast<int>(std::sqrt(size)), static_cast<int>(std::sqrt(size))};
    while (shape[0] * shape[1] != size) {
        shape[1] += 1;
        if (shape[0] * shape[1] > size) {
            shape[0] += 1;
            shape[1] = size / shape[0];
        }
    }

    auto cgamma = c_gamma(res, shape);
    std::vector<std::vector<std::complex<double>>> kz(batch_size, std::vector<std::complex<double>>(size));
    std::vector<std::vector<std::complex<double>>> eva(batch_size, std::vector<std::complex<double>>(size));

    double pi = 3.141592653589793;
    for (int b = 0; b < batch_size; ++b) {
        for (size_t i = 0; i < size; ++i) {
            kz[b][i] = std::complex<float>(2.0f * pi * res[2]) * cgamma[0][i];
            eva[b][i] = std::exp(std::min(std::abs((cgamma[0][i] - std::complex<float>(0.2)) * 5.0f), 0.0f));
        }
    }

    // Calculate p_mat
    std::vector<std::vector<std::vector<std::complex<double>>>> p_mat(4,
        std::vector<std::vector<std::complex<double>>>(batch_size, 
            std::vector<std::complex<double>>(size)));

    for (int b = 0; b < batch_size; ++b) {
        for (size_t i = 0; i < size; ++i) {
            std::complex<double> kz_val = kz[b][i];
            p_mat[0][b][i] = std::cos(kz_val * std::complex<double>(dz));
            p_mat[1][b][i] = std::sin(kz_val * std::complex<double>(dz)) / (kz_val + 1e-8);
            p_mat[2][b][i] = -std::sin(kz_val * std::complex<double>(dz)) * (kz_val);
            p_mat[3][b][i] = std::cos(kz_val * std::complex<double>(dz));
            
            // Apply eva scaling
            for (int j = 0; j < 4; ++j) {
                p_mat[j][b][i] *= eva[b][i];
            }
        }
    }

    // Calculate uf_new and ub_new
    std::vector<std::vector<std::complex<double>>> uf_new(batch_size, std::vector<std::complex<double>>(size));
    std::vector<std::vector<std::complex<double>>> ub_new(batch_size, std::vector<std::complex<double>>(size));

    for (int b = 0; b < batch_size; ++b) {
        for (size_t i = 0; i < size; ++i) {
            uf_new[b][i] = p_mat[0][b][i] * uf[b][i] + p_mat[1][b][i] * ub[b][i];
            ub_new[b][i] = p_mat[2][b][i] * uf[b][i] + p_mat[3][b][i] * ub[b][i];
        }
    }

    return {uf_new, ub_new};
}