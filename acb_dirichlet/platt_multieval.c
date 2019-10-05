/*
    Copyright (C) 2019 D.H.J Polymath

    This file is part of Arb.

    Arb is free software: you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License (LGPL) as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.  See <http://www.gnu.org/licenses/>.
*/

#include "acb_dirichlet.h"
#include "arb_hypgeom.h"
#include "acb_calc.h"
#include "acb_dft.h"


static void
_arb_add_d(arb_t z, const arb_t x, double d, slong prec)
{
    arb_t u;
    arb_init(u);
    arb_set_d(u, d);
    arb_add(z, x, u, prec);
    arb_clear(u);
}

static void
_arb_inv_si(arb_t z, slong n, slong prec)
{
    arb_set_si(z, n);
    arb_inv(z, z, prec);
}

static void
platt_g_gamma_term(acb_t out, const arb_t t0, const acb_t t, slong prec)
{
    acb_t z;
    acb_init(z);
    acb_add_arb(z, t, t0, prec);
    acb_mul_onei(z, z);
    acb_mul_2exp_si(z, z, 1);
    acb_add_ui(z, z, 1, prec);
    acb_mul_2exp_si(z, z, -2);
    acb_gamma(out, z, prec);
    acb_clear(z);
}

static void
platt_g_exp_term(acb_t out,
        const arb_t t0, const arb_t h, const acb_t t, slong prec)
{
    arb_t pi;
    acb_t z1, z2;
    arb_init(pi);
    acb_init(z1);
    acb_init(z2);
    arb_const_pi(pi, prec);
    acb_add_arb(z1, t, t0, prec);
    acb_mul_arb(z1, z1, pi, prec);
    acb_mul_2exp_si(z1, z1, -2);
    acb_div_arb(z2, t, h, prec);
    acb_sqr(z2, z2, prec);
    acb_mul_2exp_si(z2, z2, -1);
    acb_sub(out, z1, z2, prec);
    acb_exp(out, out, prec);
    arb_clear(pi);
    acb_clear(z1);
    acb_clear(z2);
}

static void
platt_g_base(acb_t out, const acb_t t, slong prec)
{
    arb_t pi;
    arb_init(pi);
    arb_const_pi(pi, prec);
    acb_mul_arb(out, t, pi, prec);
    acb_mul_onei(out, out);
    acb_mul_2exp_si(out, out, 1);
    acb_neg(out, out);
    arb_clear(pi);
}


static void
platt_g_table(acb_ptr table, slong A, slong B,
        const arb_t t0, const arb_t h, slong K, slong prec)
{
    slong N = A*B;
    slong i, n, k;
    acb_t t, base;
    acb_t gamma_term, exp_term, coeff;
    acb_ptr precomputed_powers;

    acb_init(t);
    acb_init(base);
    acb_init(gamma_term);
    acb_init(exp_term);
    acb_init(coeff);

    precomputed_powers = _acb_vec_init(K);

    for (i=0; i<N; i++)
    {
        n = i - N/2;

        acb_set_si(t, n);
        acb_div_si(t, t, A, prec);

        platt_g_base(base, t, prec);
        _acb_vec_set_powers(precomputed_powers, base, K, prec);

        platt_g_gamma_term(gamma_term, t0, t, prec);
        platt_g_exp_term(exp_term, t0, h, t, prec);
        acb_mul(coeff, gamma_term, exp_term, prec);

        for (k=0; k<K; k++)
        {
            acb_mul(table + k*N + i, coeff, precomputed_powers + k, prec);
        }
    }

    acb_clear(t);
    acb_clear(base);
    acb_clear(gamma_term);
    acb_clear(exp_term);
    acb_clear(coeff);

    _acb_vec_clear(precomputed_powers, K);
}

static void
logjsqrtpi(arb_t out, slong j, slong prec)
{
    arb_const_sqrt_pi(out, prec);
    arb_mul_si(out, out, j, prec);
    arb_log(out, out, prec);
}

static void
_acb_add_error_arb_mag(acb_t res, const arb_t x)
{
    mag_t err;
    mag_init(err);
    arb_get_mag(err, x);
    acb_add_error_mag(res, err);
    mag_clear(err);
}

static void
_acb_vec_scalar_add_error_mag(acb_ptr res, slong len, const mag_t err)
{
    slong i;
    for (i = 0; i < len; i++)
    {
        acb_add_error_mag(res + i, err);
    }
}

static void
_acb_vec_scalar_add_error_arb_mag(acb_ptr res, slong len, const arb_t x)
{
    mag_t err;
    mag_init(err);
    arb_get_mag(err, x);
    _acb_vec_scalar_add_error_mag(res, len, err);
    mag_clear(err);
}


static void
get_smk_index(slong *out, slong B, slong j, slong prec)
{
    arb_t pi, x;
    fmpz_t z;

    arb_init(pi);
    arb_init(x);
    fmpz_init(z);

    while (1)
    {
        arb_const_pi(pi, prec);
        logjsqrtpi(x, j, prec);
        arb_div(x, x, pi, prec);
        arb_mul_2exp_si(x, x, -1);
        arb_mul_si(x, x, B, prec);
        _arb_add_d(x, x, 0.5, prec);
        arb_floor(x, x, prec);

        if (arb_get_unique_fmpz(z, x))
        {
            *out = fmpz_get_si(z);
            break;
        }
        else
        {
            prec *= 2;
        }
    }

    arb_clear(pi);
    arb_clear(x);
    fmpz_clear(z);
}


static void
platt_smk(acb_ptr table,
        const arb_t t0, slong A, slong B, slong J, slong K, slong prec)
{
    slong j, k, m;
    slong N = A * B;
    acb_ptr row;
    arb_ptr diff_powers;
    arb_t rpi, rsqrtj, um, a, base;
    acb_t z;

    arb_init(rpi);
    arb_init(rsqrtj);
    arb_init(um);
    arb_init(a);
    arb_init(base);
    acb_init(z);
    diff_powers = _arb_vec_init(K);

    arb_const_pi(rpi, prec);
    arb_inv(rpi, rpi, prec);

    for (j = 1; j <= J; j++)
    {
        logjsqrtpi(a, j, prec);
        arb_mul(a, a, rpi, prec);

        arb_rsqrt_ui(rsqrtj, (ulong) j, prec);

        acb_set_arb(z, t0);
        acb_mul_arb(z, z, a, prec);
        acb_neg(z, z);
        acb_exp_pi_i(z, z, prec);
        acb_mul_arb(z, z, rsqrtj, prec);

        get_smk_index(&m, B, j, prec);

        arb_set_si(um, m);
        arb_div_si(um, um, B, prec);

        arb_mul_2exp_si(base, a, -1);
        arb_sub(base, base, um, prec);

        _arb_vec_set_powers(diff_powers, base, K, prec);

        for (k = 0; k < K; k++)
        {
            row = table + N*k;
            acb_addmul_arb(row + m, z, diff_powers + k, prec);
        }
    }

    arb_clear(rpi);
    arb_clear(rsqrtj);
    arb_clear(um);
    arb_clear(a);
    arb_clear(base);
    acb_clear(z);
    _arb_vec_clear(diff_powers, K);
}


static void
do_convolutions(acb_ptr out_table,
        const acb_ptr table, const acb_ptr S_table,
        slong N, slong K, slong prec)
{
    slong i, k;
    acb_ptr padded_table_row, padded_S_table_row, padded_out_table;
    acb_ptr fp, gp;
    acb_dft_pre_t pre;

    padded_table_row = _acb_vec_init(N*2);
    padded_S_table_row = _acb_vec_init(N*2);
    padded_out_table = _acb_vec_init(N*2);
    fp = _acb_vec_init(N*2);
    gp = _acb_vec_init(N*2);

    acb_dft_precomp_init(pre, N*2, prec);

    for (k = 0; k < K; k++)
    {
        _acb_vec_zero(padded_table_row, N*2);
        _acb_vec_zero(padded_S_table_row, N*2);
        _acb_vec_zero(padded_out_table, N*2);

        _acb_vec_set(padded_table_row, table + k*N, N);
        _acb_vec_set(padded_S_table_row, S_table + k*N, N);

        for (i = 1; i < N; i++)
        {
            acb_swap(padded_S_table_row + i, padded_S_table_row + N*2 - i);
        }

        acb_dft_precomp(fp, padded_S_table_row, pre, prec);
        acb_dft_precomp(gp, padded_table_row, pre, prec);
        _acb_vec_kronecker_mul(gp, gp, fp, N*2, prec);
        acb_dft_inverse_precomp(padded_out_table, gp, pre, prec);

        for (i = 0; i <= N/2; i++)
        {
            acb_add(out_table + i,
                    out_table + i, padded_out_table + i, prec);
        }
    }

    _acb_vec_clear(padded_table_row, N*2);
    _acb_vec_clear(padded_S_table_row, N*2);
    _acb_vec_clear(padded_out_table, N*2);
    _acb_vec_clear(fp, N*2);
    _acb_vec_clear(gp, N*2);

    acb_dft_precomp_clear(pre);
}

static void
remove_gaussian_window(arb_ptr out, slong A, slong B, const arb_t h, slong prec)
{
    slong i, n;
    slong N = A*B;
    arb_t t, x;
    arb_init(t);
    arb_init(x);
    for (i = 0; i < N; i++)
    {
        n = i - N/2;
        arb_set_si(t, n);
        arb_div_si(t, t, A, prec);
        arb_div(x, t, h, prec);
        arb_sqr(x, x, prec);
        arb_mul_2exp_si(x, x, -1);
        arb_exp(x, x, prec);
        arb_mul(out + i, out + i, x, prec);
    }
    arb_clear(t);
    arb_clear(x);
}

void
acb_dirichlet_platt_multieval(arb_ptr out, const fmpz_t T, slong A, slong B,
        const arb_t h, slong J, slong K, slong sigma, slong prec)
{
    slong N = A*B;
    slong i, k;
    acb_ptr table, S_table, out_a, out_b;
    acb_ptr row;
    arb_t t0, t, x, k_factorial, err, ratio, c, xi;
    acb_t z;
    acb_dft_pre_t pre_N;

    arb_init(t0);
    arb_init(t);
    arb_init(x);
    arb_init(k_factorial);
    arb_init(err);
    arb_init(ratio);
    arb_init(c);
    arb_init(xi);
    acb_init(z);
    table = _acb_vec_init(K*N);
    S_table = _acb_vec_init(K*N);
    out_a = _acb_vec_init(N);
    out_b = _acb_vec_init(N);
    acb_dft_precomp_init(pre_N, N, prec);

    arb_set_fmpz(t0, T);
    _arb_inv_si(xi, B, prec);
    arb_mul_2exp_si(xi, xi, -1);

    platt_smk(S_table, t0, A, B, J, K, prec);

    platt_g_table(table, A, B, t0, h, K, prec);

    for (k = 0; k < K; k++)
    {
        acb_dirichlet_platt_lemma_A5(err, B, h, k, prec);
        _acb_vec_scalar_add_error_arb_mag(table + N*k, N, err);
    }

    for (k = 0; k < K; k++)
    {
        row = table + N*k;
        for (i = 0; i < N/2; i++)
        {
            acb_swap(row + i, row + i + N/2);
        }
        acb_dft_precomp(row, row, pre_N, prec);
    }
    _acb_vec_scalar_div_ui(table, table, N*K, (ulong) A, prec);

    for (k = 0; k < K; k++)
    {
        acb_dirichlet_platt_lemma_A7(err, sigma, t0, h, k, A, prec);
        _acb_vec_scalar_add_error_arb_mag(table + N*k, N, err);
    }

    arb_one(k_factorial);
    for (k = 2; k < K; k++)
    {
        row = table + N*k;
        arb_mul_ui(k_factorial, k_factorial, (ulong) k, prec);
        _acb_vec_scalar_div_arb(row, row, N, k_factorial, prec);
    }

    do_convolutions(out_a, table, S_table, N, K, prec);

    for (i = 0; i < N/2 + 1; i++)
    {
        arb_set_si(x, i);
        arb_div_si(x, x, B, prec);
        acb_dirichlet_platt_lemma_32(err, h, t0, x, prec);
        _acb_add_error_arb_mag(out_a + i, err);
    }

    acb_dirichlet_platt_lemma_B1(err, sigma, t0, h, J, prec);
    _acb_vec_scalar_add_error_arb_mag(out_a, N/2 + 1, err);

    arb_sqrt_ui(c, (ulong) J, prec);
    arb_mul_2exp_si(c, c, 1);
    arb_sub_ui(c, c, 1, prec);
    acb_dirichlet_platt_lemma_B2(err, K, h, xi, prec);
    arb_mul(err, err, c, prec);
    _acb_vec_scalar_add_error_arb_mag(out_a, N/2 + 1, err);

    for (i = 1; i < N/2; i++)
    {
        acb_conj(out_a + N - i, out_a + i);
    }

    acb_dirichlet_platt_lemma_A9(err, sigma, t0, h, A, prec);
    _acb_vec_scalar_add_error_arb_mag(out_a, N, err);

    acb_dft_inverse_precomp(out_b, out_a, pre_N, prec);
    _acb_vec_scalar_mul_ui(out_b, out_b, N, (ulong) A, prec);
    for (i = 0; i < N/2; i++)
    {
        acb_swap(out_b + i, out_b + i + N/2);
    }

    acb_dirichlet_platt_lemma_A11(err, t0, h, B, prec);
    _acb_vec_scalar_add_error_arb_mag(out_b, N, err);

    for (i = 0; i < N; i++)
    {
        arb_swap(out + i, acb_realref(out_b + i));
    }

    remove_gaussian_window(out, A, B, h, prec);

    arb_clear(t0);
    arb_clear(t);
    arb_clear(x);
    arb_clear(k_factorial);
    arb_clear(err);
    arb_clear(ratio);
    arb_clear(c);
    arb_clear(xi);
    acb_clear(z);
    _acb_vec_clear(table, K*N);
    _acb_vec_clear(S_table, K*N);
    _acb_vec_clear(out_a, N);
    _acb_vec_clear(out_b, N);
    acb_dft_precomp_clear(pre_N);
}