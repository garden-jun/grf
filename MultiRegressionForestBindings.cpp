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

#include <Rcpp.h>
#include <vector>

#include "commons/globals.h"
#include "forest/ForestPredictors.h"
#include "forest/ForestTrainers.h"
#include "RcppUtilities.h"

#include <time.h>

using namespace grf;

// [[Rcpp::export]]
Rcpp::List multi_regression_train(const Rcpp::NumericMatrix& train_matrix,
                                  const std::vector<size_t>& outcome_index,
                                  size_t sample_weight_index,
                                  bool use_sample_weights,
                                  unsigned int mtry,
                                  unsigned int num_trees,
                                  unsigned int min_node_size,
                                  double sample_fraction,
                                  bool honesty,
                                  double honesty_fraction,
                                  bool honesty_prune_leaves,
                                  double alpha,
                                  double imbalance_penalty,
                                  std::vector<size_t>& clusters,
                                  unsigned int samples_per_cluster,
                                  bool compute_oob_predictions,
                                  unsigned int num_threads,
                                  unsigned int seed) {
  clock_t  start, end;
  start = clock();
  Data data = RcppUtilities::convert_data(train_matrix);
  data.set_outcome_index(outcome_index);
  if (use_sample_weights) {
    data.set_weight_index(sample_weight_index);
  }

  size_t ci_group_size = 1;
  ForestOptions options(num_trees, ci_group_size, sample_fraction, mtry, min_node_size, honesty,
      honesty_fraction, honesty_prune_leaves, alpha, imbalance_penalty, num_threads, seed, clusters, samples_per_cluster);
  ForestTrainer trainer = multi_regression_trainer(data.get_num_outcomes());
  Forest forest = trainer.train(data, options);

  std::vector<Prediction> predictions;
  if (compute_oob_predictions) {
    ForestPredictor predictor = multi_regression_predictor(num_threads, data.get_num_outcomes());
    predictions = predictor.predict_oob(forest, data, false);
  }
  end = clock();

  Rcpp::Rcout << "operating time : " << static_cast<double>(end - start) << "ms" << '\n';
  return RcppUtilities::create_forest_object(forest, predictions);
}

// [[Rcpp::export]]
Rcpp::List multi_regression_predict(const Rcpp::List& forest_object,
                                    const Rcpp::NumericMatrix& train_matrix,
                                    const Rcpp::NumericMatrix& test_matrix,
                                    size_t num_outcomes,
                                    unsigned int num_threads) {
  Data train_data = RcppUtilities::convert_data(train_matrix);

  Data data = RcppUtilities::convert_data(test_matrix);
  Forest forest = RcppUtilities::deserialize_forest(forest_object);
  bool estimate_variance = false;
  ForestPredictor predictor = multi_regression_predictor(num_threads, num_outcomes);
  std::vector<Prediction> predictions = predictor.predict(forest, train_data, data, estimate_variance);

  return RcppUtilities::create_prediction_object(predictions);
}

// [[Rcpp::export]]
Rcpp::List multi_regression_predict_oob(const Rcpp::List& forest_object,
                                        const Rcpp::NumericMatrix& train_matrix,
                                        size_t num_outcomes,
                                        unsigned int num_threads) {
  Data data = RcppUtilities::convert_data(train_matrix);

  Forest forest = RcppUtilities::deserialize_forest(forest_object);
  bool estimate_variance = false;
  ForestPredictor predictor = multi_regression_predictor(num_threads, num_outcomes);
  std::vector<Prediction> predictions = predictor.predict_oob(forest, data, estimate_variance);

  Rcpp::List result = RcppUtilities::create_prediction_object(predictions);
  return result;
}

/*** R
data = read.csv("D:/R/grf/dat_multiRF.csv")
X = data[3:12]
Y = data[1:2]
forest = multi_regression_forest(X,Y,1, honesty=F)
new_data = matrix(c(0.791961346, -0.547185228, -0.597154391, -0.385432707, -0.660770696, 0.536028078, 0.283616233, -0.179933712, -0.020123282, -0.735890979), nrow=1, ncol=10)
# 0.791961346	-0.547185228	-0.597154391	-0.385432707	-0.660770696	0.536028078	0.283616233	-0.179933712	-0.020123282	-0.735890979

outcome = predict.multi_regression_forest(forest, new_data)
outcome
*/