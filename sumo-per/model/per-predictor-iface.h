#pragma once
#include <string>
#include <unordered_map>

/**
 * Optional interface for online PER prediction.
 * Keep it lightweight: you can implement it using ONNX Runtime, libtorch, or a simple model.
 *
 * Inputs are feature name → value. Implementations must return a finite
 * predicted PER in [0,1]; callers should validate the returned value.
 */
class PerPredictorIface
{
public:
  virtual ~PerPredictorIface() = default;
  virtual double Predict(const std::unordered_map<std::string, double>& features) = 0;
};
