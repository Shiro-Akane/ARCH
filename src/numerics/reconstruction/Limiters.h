/**
 * @file Limiters.h
 * @brief Slope limiters for MUSCL reconstruction.
 * * * Defines the slope limiter functions phi(r).
 * r is the ratio of successive gradients: r = (u_i - u_{i-1}) / (u_{i+1} - u_i)
 */

#pragma once

#include <algorithm>
#include <string>
#include <cmath>

/**
 * @struct NoLimiter
 * @brief 不使用限制器 (退化为一阶精度，或者无限制的二阶).
 * 通常如果不限制，二阶中心差分对应 phi(r) = 1 (Lax-Wendroff) 或 phi(r) = r (Beam-Warming).
 * 这里作为 "First Order" 的占位符，phi(r) = 0 表示完全丢弃梯度，只用均值。
 */
struct NoLimiter
{
    static std::string name() { return "None (1st Order)"; }

    static double calc(double r)
    {
        return 0.0;
    }
};

/**
 * @struct MinMod
 * @brief 最常用的耗散型限制器 (Diffusive but stable).
 * phi(r) = max(0, min(1, r))
 */
struct MinMod
{
    static std::string name() { return "MinMod"; }

    static double calc(double r)
    {
        return (r > 0.0) ? (r < 1.0 ? r : 1.0) : 0.0;
    }
};

/**
 * @struct SuperBee
 * @brief 压缩型限制器 (Compressive).
 * phi(r) = max(0, min(2r, 1), min(r, 2))
 * 特点：对接触间断（Contact Discontinuity）分辨最好，最锐利，但可能把正弦波变成方波。
 */
struct SuperBee
{
    static std::string name() { return "SuperBee"; }

    static double calc(double r)
    {
        if (r <= 0.0)
            return 0.0;
        if (r >= 2.0)
            return 2.0; // max limit
        if (r <= 0.5)
            return 2.0 * r;
        if (r >= 1.0)
            return r; // Between 1 and 2, choose r (wait, formula is complex)

        // 标准公式: max(0, min(1, 2r), min(2, r))
        double a = (2.0 * r < 1.0) ? 2.0 * r : 1.0;
        double b = (r < 2.0) ? r : 2.0;
        return (a > b) ? a : b;
    }
};

/**
 * @struct VanLeer
 * @brief 平滑限制器 (Smooth).
 * phi(r) = (r + |r|) / (1 + |r|)
 * 特点：通常被认为是 MinMod 和 SuperBee 之间的最佳折衷方案。
 */
struct VanLeer
{
    static std::string name() { return "VanLeer"; }

    static double calc(double r)
    {
        // 优化写法：如果 r<=0, phi=0
        if (r <= 0.0)
            return 0.0;
        return (2.0 * r) / (1.0 + r);
    }
};

/**
 * @struct McLimiter
 * @brief Monotonized Central (MC) Limiter.
 * phi(r) = max(0, min(2r, 0.5*(1+r), 2))
 * 特点：对于平滑区域，它近似于三阶精度。
 */
struct McLimiter
{
    static std::string name() { return "MC"; }

    static double calc(double r)
    {
        if (r <= 0.0)
            return 0.0;

        double term2 = 0.5 * (1.0 + r);
        double term1 = 2.0 * r;
        double term3 = 2.0;

        // min(term1, term2, term3)
        double min_val = (term1 < term2) ? term1 : term2;
        min_val = (min_val < term3) ? min_val : term3;

        return min_val;
    }
};