/**
 * Copyright (C) 2016 D Levin (http://www.kfrlib.com)
 * This file is part of KFR
 *
 * KFR is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * KFR is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with KFR.
 *
 * If GPL is not suitable for your project, you must purchase a commercial license to use KFR.
 * Buying a commercial license is mandatory as soon as you develop commercial activities without
 * disclosing the source code of your own applications.
 * See http://www.kfrlib.com for details.
 */
#pragma once

#include "../base/function.hpp"
#include "../base/log_exp.hpp"
#include "../base/select.hpp"
#include "../base/sin_cos.hpp"
#include "../base/vec.hpp"

#pragma clang diagnostic push
#if CID_HAS_WARNING("-Winaccessible-base")
#pragma clang diagnostic ignored "-Winaccessible-base"
#endif

namespace kfr
{

namespace internal
{

template <cpu_t cpu = cpu_t::native>
struct in_generators : in_log_exp<cpu>, in_select<cpu>, in_sin_cos<cpu>
{
private:
    using in_log_exp<cpu>::exp;
    using in_log_exp<cpu>::exp2;
    using in_select<cpu>::select;
    using in_sin_cos<cpu>::cossin;

public:
    template <typename T, size_t width_, typename Class>
    struct generator
    {
        constexpr static size_t width = width_;
        using type                    = T;

        template <typename U, size_t N>
        KFR_INLINE vec<U, N> operator()(cinput_t, size_t, vec_t<U, N> t) const
        {
            return cast<U>(generate(t));
        }

        void resync(T start) const { ptr_cast<Class>(this)->sync(start); }

    protected:
        void call_next() const { ptr_cast<Class>(this)->next(); }
        template <size_t N>
        void call_shift(csize_t<N>) const
        {
            ptr_cast<Class>(this)->shift(csize<N>);
        }

        template <size_t N>
        void shift(csize_t<N>) const
        {
            const vec<T, width> oldvalue = value;
            call_next();
            value = slice<N, width>(oldvalue, value);
        }

        template <size_t N, KFR_ENABLE_IF(N == width)>
        KFR_INLINE vec<T, N> generate(vec_t<T, N>) const
        {
            const vec<T, N> result = value;
            call_next();
            return result;
        }

        template <size_t N, KFR_ENABLE_IF(N < width)>
        KFR_INLINE vec<T, N> generate(vec_t<T, N>) const
        {
            const vec<T, N> result = narrow<N>(value);
            shift(csize<N>);
            return result;
        }

        template <size_t N, KFR_ENABLE_IF(N > width)>
        KFR_INLINE vec<T, N> generate(vec_t<T, N> x) const
        {
            const auto lo = generate(low(x));
            const auto hi = generate(high(x));
            return concat(lo, hi);
        }

        mutable vec<T, width> value;
    };

    template <typename T, size_t width = get_vector_width<T, cpu>(1, 2)>
    struct generator_linear : generator<T, width, generator_linear<T, width>>
    {
        template <cpu_t newcpu>
        using retarget_this = typename in_generators<newcpu>::template generator_linear<T>;

        constexpr generator_linear(T start, T step) noexcept : step(step), vstep(step* width)
        {
            this->resync(start);
        }

        KFR_INLINE void sync(T start) const noexcept { this->value = start + enumerate<T, width>() * step; }

        KFR_INLINE void next() const noexcept { this->value += vstep; }

    protected:
        T step;
        T vstep;
    };

    template <typename T, size_t width = get_vector_width<T, cpu>(1, 2)>
    struct generator_exp : generator<T, width, generator_exp<T, width>>
    {
        template <cpu_t newcpu>
        using retarget_this = typename in_generators<newcpu>::template generator_exp<T>;

        generator_exp(T start, T step) noexcept : step(step), vstep(exp(make_vector(step* width))[0] - 1)
        {
            this->resync(start);
        }

        KFR_INLINE void sync(T start) const noexcept
        {
            this->value = exp(start + enumerate<T, width>() * step);
        }

        KFR_INLINE void next() const noexcept { this->value += this->value * vstep; }

    protected:
        T step;
        T vstep;
    };

    template <typename T, size_t width = get_vector_width<T, cpu>(1, 2)>
    struct generator_exp2 : generator<T, width, generator_exp2<T, width>>
    {
        template <cpu_t newcpu>
        using retarget_this = typename in_generators<newcpu>::template generator_exp2<T>;

        generator_exp2(T start, T step) noexcept : step(step), vstep(exp2(make_vector(step* width))[0] - 1)
        {
            this->resync(start);
        }

        KFR_INLINE void sync(T start) const noexcept
        {
            this->value = exp2(start + enumerate<T, width>() * step);
        }

        KFR_INLINE void next() const noexcept { this->value += this->value * vstep; }

    protected:
        T step;
        T vstep;
    };

    template <typename T, size_t width = get_vector_width<T, cpu>(1, 2)>
    struct generator_cossin : generator<T, width, generator_cossin<T, width>>
    {
        template <cpu_t newcpu>
        using retarget_this = typename in_generators<newcpu>::template generator_cossin<T>;

        generator_cossin(T start, T step)
            : step(step), alpha(2 * sqr(sin(width / 2 * step / 2))), beta(-sin(width / 2 * step))
        {
            this->resync(start);
        }
        KFR_INLINE void sync(T start) const noexcept { this->value = init_cossin(step, start); }

        KFR_INLINE void next() const noexcept
        {
            this->value = this->value - subadd(alpha * this->value, beta * swap<2>(this->value));
        }

    protected:
        T step;
        T alpha;
        T beta;
        KFR_NOINLINE static vec<T, width> init_cossin(T w, T phase)
        {
            return cossin(dup(phase + enumerate<T, width / 2>() * w));
        }
    };

    template <typename T, size_t width = get_vector_width<T, cpu>(2, 4)>
    struct generator_sin : generator<T, width, generator_sin<T, width>>
    {
        template <cpu_t newcpu>
        using retarget_this = typename in_generators<newcpu>::template generator_sin<T>;

        generator_sin(T start, T step)
            : step(step), alpha(2 * sqr(sin(width * step / 2))), beta(sin(width * step))
        {
            this->resync(start);
        }
        KFR_INLINE void sync(T start) const noexcept
        {
            const vec<T, width* 2> cs = splitpairs(cossin(dup(start + enumerate<T, width>() * step)));
            this->cos_value = low(cs);
            this->value     = high(cs);
        }

        KFR_INLINE void next() const noexcept
        {
            const vec<T, width> c = this->cos_value;
            const vec<T, width> s = this->value;

            const vec<T, width> cc = alpha * c + beta * s;
            const vec<T, width> ss = alpha * s - beta * c;

            this->cos_value = c - cc;
            this->value     = s - ss;
        }

        template <size_t N>
        void shift(csize_t<N>) const noexcept
        {
            const vec<T, width> oldvalue    = this->value;
            const vec<T, width> oldcosvalue = this->cos_value;
            next();
            this->value     = slice<N, width>(oldvalue, this->value);
            this->cos_value = slice<N, width>(oldcosvalue, this->cos_value);
        }

    protected:
        T step;
        T alpha;
        T beta;
        mutable vec<T, width> cos_value;
    };
};
}

template <typename T1, typename T2, typename TF = ftype<common_type<T1, T2>>>
KFR_SINTRIN internal::in_generators<>::generator_linear<TF> gen_linear(T1 start, T2 step)
{
    return internal::in_generators<>::generator_linear<TF>(start, step);
}
template <typename T1, typename T2, typename TF = ftype<common_type<T1, T2>>>
KFR_SINTRIN internal::in_generators<>::generator_exp<TF> gen_exp(T1 start, T2 step)
{
    return internal::in_generators<>::generator_exp<TF>(start, step);
}
template <typename T1, typename T2, typename TF = ftype<common_type<T1, T2>>>
KFR_SINTRIN internal::in_generators<>::generator_exp2<TF> gen_exp2(T1 start, T2 step)
{
    return internal::in_generators<>::generator_exp2<TF>(start, step);
}
template <typename T1, typename T2, typename TF = ftype<common_type<T1, T2>>>
KFR_SINTRIN internal::in_generators<>::generator_sin<TF> gen_cossin(T1 start, T2 step)
{
    return internal::in_generators<>::generator_cossin<TF>(start, step);
}
template <typename T1, typename T2, typename TF = ftype<common_type<T1, T2>>>
KFR_SINTRIN internal::in_generators<>::generator_sin<TF> gen_sin(T1 start, T2 step)
{
    return internal::in_generators<>::generator_sin<TF>(start, step);
}
}

#pragma clang diagnostic pop
