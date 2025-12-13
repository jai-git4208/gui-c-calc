#ifndef MODEL_H
#define MODEL_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define INPUT_NODES 784
#define HIDDEN_NODES 128
#define OUTPUT_NODES 10

typedef struct {
  float w1[INPUT_NODES][HIDDEN_NODES];
  float b1[HIDDEN_NODES];
  float w2[HIDDEN_NODES][OUTPUT_NODES];
  float b2[OUTPUT_NODES];
} NeuralNetwork;

// Load model from file
// Returns 1 on success, 0 on failure
static int model_load(const char *filename, NeuralNetwork *nn) {
  FILE *f = fopen(filename, "rb");
  if (!f)
    return 0;

  size_t read = 0;
  read += fread(nn->w1, sizeof(float), INPUT_NODES * HIDDEN_NODES, f);
  read += fread(nn->b1, sizeof(float), HIDDEN_NODES, f);
  read += fread(nn->w2, sizeof(float), HIDDEN_NODES * OUTPUT_NODES, f);
  read += fread(nn->b2, sizeof(float), OUTPUT_NODES, f);

  fclose(f);

  size_t expected = INPUT_NODES * HIDDEN_NODES + HIDDEN_NODES +
                    HIDDEN_NODES * OUTPUT_NODES + OUTPUT_NODES;

  return (read == expected);
}

// Inference
// Returns predicted digit (0-9)
static int model_predict(NeuralNetwork *nn, const float *input) {
  float hidden[HIDDEN_NODES];
  float output[OUTPUT_NODES];

  // Layer 1: Forward
  for (int i = 0; i < HIDDEN_NODES; i++) {
    float sum = nn->b1[i];
    for (int j = 0; j < INPUT_NODES; j++) {
      sum += input[j] * nn->w1[j][i];
    }
    // ReLU Activation
    hidden[i] = sum > 0 ? sum : 0;
  }

  // Layer 2: Forward
  for (int i = 0; i < OUTPUT_NODES; i++) {
    float sum = nn->b2[i];
    for (int j = 0; j < HIDDEN_NODES; j++) {
      sum += hidden[j] * nn->w2[j][i];
    }
    output[i] = sum; // We can use logits directly for argmax
  }

  // Argmax
  int maxIdx = 0;
  float maxVal = output[0];
  for (int i = 1; i < OUTPUT_NODES; i++) {
    if (output[i] > maxVal) {
      maxVal = output[i];
      maxIdx = i;
    }
  }

  return maxIdx;
}

#endif
