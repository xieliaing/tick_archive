// License: BSD 3 clause


#include "hawkes_fixed_expkern_loglik.h"

ModelHawkesFixedExpKernLogLik::ModelHawkesFixedExpKernLogLik(
    const double decay, const int max_n_threads) :
    ModelHawkesSingle(max_n_threads, 0),
    decay(decay) {}

void ModelHawkesFixedExpKernLogLik::compute_weights() {
  allocate_weights();
  parallel_run(get_n_threads(), n_nodes, &ModelHawkesFixedExpKernLogLik::compute_weights_dim_i, this);
  weights_computed = true;

  for (ulong k = 0; k < n_nodes; ++k) {
    std::cout << "\nsum_G " << k
              << ", min sum_G(k)=" << sum_G[k].min()
              << ", max sum_G(k)=" << sum_G[k].max()
              << std::endl;
    sum_G[k].print();
  }
}

void ModelHawkesFixedExpKernLogLik::allocate_weights() {
  if (n_nodes == 0) {
    TICK_ERROR("Please provide valid timestamps before allocating weights")
  }
  g = ArrayDouble2dList1D(n_nodes);
  G = ArrayDouble2dList1D(n_nodes);
  sum_G = ArrayDoubleList1D(n_nodes);

  for (ulong i = 0; i < n_nodes; i++) {
    g[i] = ArrayDouble2d((*n_jumps_per_node)[i], n_nodes);
    g[i].init_to_zero();
    G[i] = ArrayDouble2d((*n_jumps_per_node)[i] + 1, n_nodes);
    G[i].init_to_zero();
    sum_G[i] = ArrayDouble(n_nodes);
  }
}

void ModelHawkesFixedExpKernLogLik::compute_weights_dim_i(const ulong i) {
  const ArrayDouble t_i = view(*timestamps[i]);
  ArrayDouble2d g_i = view(g[i]);
  ArrayDouble2d G_i = view(G[i]);
  ArrayDouble sum_G_i = view(sum_G[i]);

  const ulong n_jumps_i = (*n_jumps_per_node)[i];

  for (ulong j = 0; j < n_nodes; j++) {
    const ArrayDouble t_j = view(*timestamps[j]);
    ulong ij = 0;
    for (ulong k = 0; k < n_jumps_i + 1; k++) {
      const double t_i_k = k < n_jumps_i ? t_i[k] : end_time;
      if (k > 0) {
        const double ebt = std::exp(-decay * (t_i_k - t_i[k - 1]));

        if (k < n_jumps_i) g_i[k * n_nodes + j] = g_i[(k - 1) * n_nodes + j] * ebt;
        G_i[k * n_nodes + j] = g_i[(k - 1) * n_nodes + j] * (1 - ebt) / decay;
      } else {
        g_i[k * n_nodes + j] = 0;
        G_i[k * n_nodes + j] = 0;
        sum_G[i][j] = 0.;
      }

      while ((ij < (*n_jumps_per_node)[j]) && (t_j[ij] < t_i_k)) {
        const double ebt = std::exp(-decay * (t_i_k - t_j[ij]));
        if (k < n_jumps_i) g_i[k * n_nodes + j] += decay * ebt;
        G_i[k * n_nodes + j] += 1 - ebt;
        ij++;
      }
      sum_G_i[j] += G_i[k * n_nodes + j];
//      std::cout << "i=" << i << ", j=" << j
//                //                  << ", adds : " << G_i[get_index(k, j, u)]
//                << "sum_G_i[j]=" << sum_G_i[j]
//                << std::endl;
    }
  }
}

double ModelHawkesFixedExpKernLogLik::loss(const ArrayDouble &coeffs) {
  if (!weights_computed) compute_weights();

  const double loss =
      parallel_map_additive_reduce(get_n_threads(), n_nodes,
                                   &ModelHawkesFixedExpKernLogLik::loss_dim_i,
                                   this,
                                   coeffs);
  return loss / n_total_jumps;
}

double ModelHawkesFixedExpKernLogLik::loss_i(const ulong sampled_i,
                                             const ArrayDouble &coeffs) {
  if (!weights_computed) compute_weights();
  ulong i;
  ulong k;
  sampled_i_to_index(sampled_i, &i, &k);

  return loss_i_k(i, k, coeffs);
}

void ModelHawkesFixedExpKernLogLik::grad(const ArrayDouble &coeffs,
                                         ArrayDouble &out) {
  if (!weights_computed) compute_weights();
  out.fill(0);

  // This allows to run in a multithreaded environment the computation of each component
  parallel_run(get_n_threads(), n_nodes, &ModelHawkesFixedExpKernLogLik::grad_dim_i, this, coeffs, out);
  out /= n_total_jumps;
}

void ModelHawkesFixedExpKernLogLik::grad_i(const ulong sampled_i,
                                           const ArrayDouble &coeffs,
                                           ArrayDouble &out) {
  if (!weights_computed) compute_weights();

  ulong i;
  ulong k;
  sampled_i_to_index(sampled_i, &i, &k);

  // set grad to zero
  out.fill(0);

  grad_i_k(i, k, coeffs, out);
}

double ModelHawkesFixedExpKernLogLik::loss_and_grad(const ArrayDouble &coeffs,
                                                    ArrayDouble &out) {
  if (!weights_computed) compute_weights();
  out.fill(0);

  const double loss =
      parallel_map_additive_reduce(get_n_threads(), n_nodes,
                                   &ModelHawkesFixedExpKernLogLik::loss_and_grad_dim_i,
                                   this,
                                   coeffs, out);
  out /= n_total_jumps;
  return loss / n_total_jumps;
}

double ModelHawkesFixedExpKernLogLik::hessian_norm(const ArrayDouble &coeffs,
                                                   const ArrayDouble &vector) {
  if (!weights_computed) compute_weights();

  const double norm_sum =
      parallel_map_additive_reduce(get_n_threads(), n_nodes,
                                   &ModelHawkesFixedExpKernLogLik::hessian_norm_dim_i,
                                   this,
                                   coeffs, vector);

  return norm_sum / n_total_jumps;
}


void ModelHawkesFixedExpKernLogLik::hessian(const ArrayDouble &coeffs, ArrayDouble &out) {
  if (!weights_computed) compute_weights();

  // This allows to run in a multithreaded environment the computation of each component
  parallel_run(get_n_threads(), n_nodes, &ModelHawkesFixedExpKernLogLik::hessian_i,
               this, coeffs, out);
  out /= n_total_jumps;
}



////////////////////////////////////////////////////////////////////////////////////////////////////
//                                    PRIVATE METHODS
////////////////////////////////////////////////////////////////////////////////////////////////////

void ModelHawkesFixedExpKernLogLik::sampled_i_to_index(const ulong sampled_i,
                                                       ulong *i,
                                                       ulong *k) {
  ulong cum_N_i = 0;
  for (ulong d = 0; d < n_nodes; d++) {
    cum_N_i += (*n_jumps_per_node)[d];
    if (sampled_i < cum_N_i) {
      *i = d;
      *k = sampled_i - cum_N_i + (*n_jumps_per_node)[d];
      break;
    }
  }
}

double ModelHawkesFixedExpKernLogLik::loss_dim_i(const ulong i,
                                                 const ArrayDouble &coeffs) {
  const double mu_i = coeffs[i];
  const ArrayDouble alpha_i = view(coeffs, get_alpha_i_first_index(i), get_alpha_i_last_index(i));

  double loss = 0;
  loss += end_time * mu_i;

  for (ulong k = 0; k < (*n_jumps_per_node)[i]; ++k) {
    const ArrayDouble g_i_k = view_row(g[i], k);

    double s = mu_i;
    s += alpha_i.dot(g_i_k);
    if (s <= 0) {
      TICK_ERROR("The sum of the influence on someone cannot be negative. "
                     "Maybe did you forget to add a positive constraint to "
                     "your proximal operator");
    }
    loss -= log(s);
  }

  loss += alpha_i.dot(sum_G[i]);
  return loss;
}

double ModelHawkesFixedExpKernLogLik::loss_i_k(const ulong i,
                                               const ulong k,
                                               const ArrayDouble &coeffs) {
  const double mu_i = coeffs[i];
  const ArrayDouble alpha_i = view(coeffs, get_alpha_i_first_index(i), get_alpha_i_last_index(i));
  double loss = 0;

  const ArrayDouble g_i_k = view_row(g[i], k);
  const ArrayDouble G_i_k = view_row(G[i], k);

  // Both are correct, just a question of point of view
  const double t_i_k = k == (*n_jumps_per_node)[i] - 1 ? end_time : (*timestamps[i])[k];
  const double t_i_k_minus_one = k == 0 ? 0 : (*timestamps[i])[k - 1];
  loss += (t_i_k - t_i_k_minus_one) * mu_i;
  //  loss += end_time * mu[i] / (*n_jumps_per_node)[i];

  double s = mu_i;
  s += alpha_i.dot(g_i_k);

  if (s <= 0) {
    TICK_ERROR("The sum of the influence on someone cannot be negative. Maybe did "
                   "you forget to add a positive constraint to your "
                   "proximal operator");
  }
  loss -= log(s);

  loss += alpha_i.dot(G_i_k);
  if (k == (*n_jumps_per_node)[i] - 1)
    loss += alpha_i.dot(view_row(G[i], k + 1));

  return loss;
}

void ModelHawkesFixedExpKernLogLik::grad_dim_i(const ulong i,
                                               const ArrayDouble &coeffs,
                                               ArrayDouble &out) {
  const double mu_i = coeffs[i];
  const ArrayDouble alpha_i = view(coeffs, get_alpha_i_first_index(i), get_alpha_i_last_index(i));

  double &grad_mu_i = out[i];
  ArrayDouble grad_alpha_i = view(out, get_alpha_i_first_index(i), get_alpha_i_last_index(i));

  grad_mu_i += end_time;

  for (ulong k = 0; k < (*n_jumps_per_node)[i]; ++k) {
    const ArrayDouble g_i_k = view_row(g[i], k);
    double s = mu_i;
    s += alpha_i.dot(g_i_k);

    grad_mu_i -= 1. / s;
    grad_alpha_i.mult_incr(g_i_k, -1. / s);
  }

  grad_alpha_i.mult_incr(sum_G[i], 1);
}

void ModelHawkesFixedExpKernLogLik::grad_i_k(const ulong i, const ulong k,
                                             const ArrayDouble &coeffs,
                                             ArrayDouble &out) {
  const double mu_i = coeffs[i];
  const ArrayDouble alpha_i = view(coeffs, get_alpha_i_first_index(i), get_alpha_i_last_index(i));

  double &grad_mu_i = out[i];
  ArrayDouble grad_alpha_i = view(out, get_alpha_i_first_index(i), get_alpha_i_last_index(i));

  const ArrayDouble g_i_k = view_row(g[i], k);
  const ArrayDouble G_i_k = view_row(G[i], k);

  // Both are correct, just a question of point of view
  const double t_i_k = k == (*n_jumps_per_node)[i] - 1 ? end_time : (*timestamps[i])[k];
  const double t_i_k_minus_one = k == 0 ? 0 : (*timestamps[i])[k - 1];
  grad_mu_i += t_i_k - t_i_k_minus_one;
  //  grad_mu[i] += end_time / (*n_jumps_per_node)[i];

  double s = mu_i;
  s += alpha_i.dot(g_i_k);

  grad_mu_i -= 1. / s;
  grad_alpha_i.mult_incr(g_i_k, -1. / s);
  grad_alpha_i.mult_incr(G_i_k, 1.);

  if (k == (*n_jumps_per_node)[i] - 1)
    grad_alpha_i.mult_incr(view_row(G[i], k + 1), 1.);
}

double ModelHawkesFixedExpKernLogLik::loss_and_grad_dim_i(const ulong i,
                                                          const ArrayDouble &coeffs,
                                                          ArrayDouble &out) {
  const double mu_i = coeffs[i];
  const ArrayDouble alpha_i = view(coeffs, get_alpha_i_first_index(i), get_alpha_i_last_index(i));

  double &grad_mu_i = out[i];
  ArrayDouble grad_alpha_i = view(out, get_alpha_i_first_index(i), get_alpha_i_last_index(i));

  double loss = 0;

  grad_mu_i += end_time;
  loss += end_time * mu_i;
  for (ulong k = 0; k < (*n_jumps_per_node)[i]; k++) {
    const ArrayDouble g_i_k = view_row(g[i], k);

    double s = mu_i;
    s += alpha_i.dot(g_i_k);

    if (s <= 0) {
      TICK_ERROR("The sum of the influence on someone cannot be negative. Maybe did "
                     "you forget to add a positive constraint to your "
                     "proximal operator");
    }
    loss -= log(s);
    grad_mu_i -= 1. / s;

    grad_alpha_i.mult_incr(g_i_k, -1. / s);
  }

  loss += alpha_i.dot(sum_G[i]);
  grad_alpha_i.mult_incr(sum_G[i], 1);

  return loss;
}

double ModelHawkesFixedExpKernLogLik::hessian_norm_dim_i(const ulong i,
                                                         const ArrayDouble &coeffs,
                                                         const ArrayDouble &vector) {
  const double mu_i = coeffs[i];
  const ArrayDouble alpha_i = view(coeffs, get_alpha_i_first_index(i), get_alpha_i_last_index(i));

  double d_mu_i = vector[i];
  ArrayDouble d_alpha_i = view(vector, get_alpha_i_first_index(i), get_alpha_i_last_index(i));

  double hess_norm = 0;

  for (ulong k = 0; k < (*n_jumps_per_node)[i]; k++) {
    const ArrayDouble g_i_k = view_row(g[i], k);

    double S = d_mu_i;
    S += d_alpha_i.dot(g_i_k);

    double s = mu_i;
    s += alpha_i.dot(g_i_k);

    double tmp = S / s;
    hess_norm += tmp * tmp;
  }
  return hess_norm;
}

void ModelHawkesFixedExpKernLogLik::hessian_i(const ulong i,
                                              const ArrayDouble &coeffs,
                                              ArrayDouble &out) {
  if (!weights_computed) TICK_ERROR("Please compute weights before calling hessian_i");

  const double mu_i = coeffs[i];
  const ArrayDouble alpha_i = view(coeffs, get_alpha_i_first_index(i), get_alpha_i_last_index(i));

  const ulong start_mu_line = i * (n_nodes + 1);
  const ulong block_start = (i + 1) * n_nodes * (n_nodes + 1);

  for (ulong k = 0; k < (*n_jumps_per_node)[i]; ++k) {
    const ArrayDouble g_i_k = view_row(g[i], k);

    double s = mu_i;
    s += alpha_i.dot(g_i_k);
    const double s_2 = s * s;

    // fill mu mu
    out[start_mu_line] += 1. / s_2;
    // fill mu alpha
    for (ulong j = 0; j < n_nodes; ++j) {
      out[start_mu_line + j + 1] += g_i_k[j] / s_2;
    }

    for (ulong l = 0; l < n_nodes; ++l) {
      const ulong start_alpha_line = block_start + l * (n_nodes + 1);
      // fill alpha mu
      out[start_alpha_line] += g_i_k[l] / s_2;
      // fill alpha square
      for (ulong m = 0; m < n_nodes; ++m) {
        out[start_alpha_line + m + 1] += g_i_k[l] * g_i_k[m] / s_2;
      }
    }
  }
}

ulong ModelHawkesFixedExpKernLogLik::get_n_coeffs() const {
  return n_nodes + n_nodes * n_nodes;
}
