/*-------------------------------------------------------------------------------
  This file is part of generalized random forest (grf).

  grf is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  grf is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with grf. If not, see <http://www.gnu.org/licenses/>.
 #-------------------------------------------------------------------------------*/

#include <algorithm>

#include "MultiRegressionSplittingRule.h"

#include <Rcpp.h>
namespace grf {
    //max_num_unique_values = 총 데이터 개수
MultiRegressionSplittingRule::MultiRegressionSplittingRule(size_t max_num_unique_values,
                                                           double alpha,
                                                           double imbalance_penalty,
                                                           size_t num_outcomes):
    alpha(alpha),
    imbalance_penalty(imbalance_penalty),
    num_outcomes(num_outcomes) {
  this->counter = new size_t[max_num_unique_values];
  this->sums = Eigen::ArrayXXd(max_num_unique_values, num_outcomes);
  this->sigma = Eigen::Matrix2d(num_outcomes, num_outcomes);
  sigma(0, 0) = 1;
  sigma(0, 1) = 0.5;
  sigma(1, 0) = 0.5;
  sigma(1, 1) = 1;
}

MultiRegressionSplittingRule::~MultiRegressionSplittingRule() {
  if (counter != nullptr) {
    delete[] counter;
  }
}

bool MultiRegressionSplittingRule::find_best_split(const Data& data,
                                                   size_t node,
                                                   const std::vector<size_t>& possible_split_vars,
                                                   const Eigen::ArrayXXd& responses_by_sample,
                                                   const std::vector<std::vector<size_t>>& samples,
                                                   std::vector<size_t>& split_vars,
                                                   std::vector<double>& split_values) {

  size_t size_node = samples[node].size();
  size_t min_child_size = std::max<size_t>(static_cast<size_t>(std::ceil(size_node * alpha)), 1uL);

  // Precompute the sum of outcomes in this node.
  Eigen::ArrayXd sum_node = Eigen::ArrayXd::Zero(num_outcomes); // num_outcomes = 서로 다른 variables Y의 개수
  for (auto& sample : samples[node]) {
    sum_node += responses_by_sample.row(sample);
  }
  // Initialize the variables to track the best split variable.
  size_t best_var = 0;
  double best_value = 0;
  double best_loss = 1e11;

  // For all possible split variables
  for (auto& var : possible_split_vars) {
    find_best_split_value(data, node, var, sum_node, size_node, min_child_size,
                          best_value, best_var, best_loss, responses_by_sample, samples);
  }

  // Stop if no good split found
  if (best_loss > 1e10) {
    return true;
  }

  // Save best values
  split_vars[node] = best_var;
  split_values[node] = best_value;
  return false;
}

void MultiRegressionSplittingRule::find_best_split_value(const Data& data,
                                                    size_t node, size_t var,
                                                    const Eigen::ArrayXd& sum_node,
                                                    size_t size_node,
                                                    size_t min_child_size,
                                                    double& best_value, size_t& best_var,
                                                    double& best_loss,
                                                    const Eigen::ArrayXXd& responses_by_sample,
                                                    const std::vector<std::vector<size_t>>& samples) {
  // sorted_samples: the node samples in increasing order (may contain duplicated Xij). Length: size_node
  std::vector<double> possible_split_values; // 실제 값인데, 중복 제외
  std::vector<size_t> sorted_samples; // 실제 index
  data.get_all_values(possible_split_values, sorted_samples, samples[node], var);

  Eigen::VectorXd mu_left = Eigen::VectorXd::Zero(num_outcomes);
  Eigen::VectorXd mu_right = Eigen::VectorXd::Zero(num_outcomes);


  // Try next variable if all equal for this
  if (possible_split_values.size() < 2) {
    return;
  }

  size_t num_splits = possible_split_values.size() - 1; // -1: we do not split at the last value
  std::fill(counter, counter + num_splits, 0);
  sums.topRows(num_splits).setZero(); // Sets the first num_splits rows to zeros.


  // Fill counter and sums buckets
  size_t split_index = 0;
  for (size_t i = 0; i < size_node - 1; i++) {
    size_t sample = sorted_samples[i];
    size_t next_sample = sorted_samples[i + 1];
    double sample_value = data.get(sample, var);

    sums.row(split_index) += responses_by_sample.row(sample);
    ++counter[split_index];

    double next_sample_value = data.get(next_sample, var);
    // if the next sample value is different, including the transition (..., NaN, Xij, ...)
    // then move on to the next bucket (all logical operators with NaN evaluates to false by default)
    if (sample_value != next_sample_value) {
      ++split_index;
    }
  }

  size_t n_left = 0;
  Eigen::ArrayXd sum_left = Eigen::ArrayXd::Zero(num_outcomes);

  // Compute decrease of impurity for each possible split
  for (size_t i = 0; i < num_splits; ++i) {
    n_left += counter[i];
    sum_left += sums.row(i);
    mu_left = sum_left / n_left;

    // Skip this split if one child is too small.
    if (n_left < min_child_size) {
      continue;
    }

    // Stop if the right child is too small.
    size_t n_right = size_node - n_left;
    if (n_right < min_child_size) {
      break;
    }
    mu_right = (sum_node - sum_left) / n_right;

    double ssl = 0;
    Eigen::VectorXd sample_difference;
    for (size_t i = 0; i < n_left; i++) {
      sample_difference = responses_by_sample.row(sorted_samples[i]);
      sample_difference = sample_difference - mu_left;
      ssl += static_cast<double>(sample_difference.transpose() * sigma.inverse() * sample_difference);
    }

    double ssr = 0;
    for (size_t i = n_left; i < size_node; i++) {
      sample_difference = responses_by_sample.row(sorted_samples[i]);
      sample_difference = sample_difference - mu_right;
      ssl += static_cast<double>(sample_difference.transpose() * sigma.inverse() * sample_difference);
    }

    double loss = n_left / size_node * ssl + n_right / size_node * ssr;

    // If better than before, use this
    if (loss < best_loss) {
      best_value = possible_split_values[i];
      best_var = var;
      best_loss = loss;
    }
  }
}

} // namespace grf
