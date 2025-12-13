#include "model.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// MNIST Data Files
#define TRAIN_IMG_PATH "dataset/train-images.idx3-ubyte"
#define TRAIN_LBL_PATH "dataset/train-labels.idx1-ubyte"

// Training Hyperparameters
#define LEARNING_RATE 0.01f
#define EPOCHS 10
#define BATCH_SIZE 1 // SGD for simplicity
#define NUM_TRAIN 60000

// Helper to flip endianness (IDX format is big-endian)
uint32_t flip_bytes(uint32_t val) {
  return ((val >> 24) & 0x000000FF) | ((val >> 8) & 0x0000FF00) |
         ((val << 8) & 0x00FF0000) | ((val << 24) & 0xFF000000);
}

// Global data buffers
uint8_t *train_images = NULL;
uint8_t *train_labels = NULL;

void load_mnist() {
  FILE *f_img = fopen(TRAIN_IMG_PATH, "rb");
  FILE *f_lbl = fopen(TRAIN_LBL_PATH, "rb");

  if (!f_img || !f_lbl) {
    printf("Error: Could not open dataset files. Check path: dataset/\n");
    exit(1);
  }

  // Read headers
  uint32_t magic, num_items, rows, cols;

  fread(&magic, 4, 1, f_img);
  fread(&num_items, 4, 1, f_img);
  fread(&rows, 4, 1, f_img);
  fread(&cols, 4, 1, f_img);

  fread(&magic, 4, 1, f_lbl);
  fread(&num_items, 4, 1, f_lbl); // Should match

  num_items = flip_bytes(num_items);

  printf("Loading %d training images...\n", num_items);

  // Allocate and read
  train_images = malloc(num_items * 784);
  train_labels = malloc(num_items);

  fread(train_images, 784, num_items, f_img);
  fread(train_labels, 1, num_items, f_lbl);

  fclose(f_img);
  fclose(f_lbl);
}

// Random float -0.5 to 0.5
float rand_weight() { return ((float)rand() / RAND_MAX) - 0.5f; }

void init_network(NeuralNetwork *nn) {
  for (int i = 0; i < INPUT_NODES; i++)
    for (int j = 0; j < HIDDEN_NODES; j++)
      nn->w1[i][j] = rand_weight() * 0.1f; // Scaled initialization

  for (int i = 0; i < HIDDEN_NODES; i++)
    nn->b1[i] = 0;

  for (int i = 0; i < HIDDEN_NODES; i++)
    for (int j = 0; j < OUTPUT_NODES; j++)
      nn->w2[i][j] = rand_weight() * 0.1f;

  for (int i = 0; i < OUTPUT_NODES; i++)
    nn->b2[i] = 0;

  printf("Network initialized.\n");
}

void train(NeuralNetwork *nn) {
  float hidden[HIDDEN_NODES];
  float output[OUTPUT_NODES];
  float grad_output[OUTPUT_NODES];
  float grad_hidden[HIDDEN_NODES];

  for (int epoch = 0; epoch < EPOCHS; epoch++) {
    double total_loss = 0;
    int correct = 0;

    // Shuffle indices would be better, but sequential is ok for this demo
    for (int i = 0; i < NUM_TRAIN; i++) {
      // 1. Prepare Input
      float input[INPUT_NODES];
      for (int k = 0; k < 784; k++) {
        input[k] = train_images[i * 784 + k] / 255.0f;
      }
      int target = train_labels[i];

      // 2. Forward Propagation
      // Layer 1
      for (int h = 0; h < HIDDEN_NODES; h++) {
        float sum = nn->b1[h];
        for (int in = 0; in < INPUT_NODES; in++) {
          sum += input[in] * nn->w1[in][h];
        }
        hidden[h] = sum > 0 ? sum : 0; // ReLU
      }

      // Layer 2
      float max_logit = -1e9;
      for (int o = 0; o < OUTPUT_NODES; o++) {
        float sum = nn->b2[o];
        for (int h = 0; h < HIDDEN_NODES; h++) {
          sum += hidden[h] * nn->w2[h][o];
        }
        output[o] = sum;
        if (output[o] > max_logit)
          max_logit = output[o];
      }

      // Softmax
      float sum_exp = 0;
      for (int o = 0; o < OUTPUT_NODES; o++) {
        output[o] = expf(output[o] - max_logit); // Stability fix
        sum_exp += output[o];
      }
      for (int o = 0; o < OUTPUT_NODES; o++) {
        output[o] /= sum_exp;
      }

      // Stats
      if (output[target] > 0)
        total_loss += -logf(output[target]);

      int pred = 0;
      float max_prob = output[0];
      for (int o = 1; o < 10; o++) {
        if (output[o] > max_prob) {
          max_prob = output[o];
          pred = o;
        }
      }
      if (pred == target)
        correct++;

      // 3. Backpropagation (Cross Entropy + Softmax)
      // grad_output = probs - target_one_hot
      for (int o = 0; o < OUTPUT_NODES; o++) {
        grad_output[o] = output[o] - (o == target ? 1.0f : 0.0f);
      }

      // Gradients for Layer 2
      for (int o = 0; o < OUTPUT_NODES; o++) {
        nn->b2[o] -= LEARNING_RATE * grad_output[o];
        for (int h = 0; h < HIDDEN_NODES; h++) {
          // Save input for Layer 1 grad calc before update?
          // No, update is fine if we use separate variable, but standard SGD
          // updates weights. Actually we need to calculate dL/dHidden BEFORE
          // updating w2.
        }
      }

      // To do it correctly:
      // Calculate error signal for hidden layer first
      for (int h = 0; h < HIDDEN_NODES; h++) {
        float sum = 0;
        for (int o = 0; o < OUTPUT_NODES; o++) {
          sum += grad_output[o] * nn->w2[h][o];
        }
        // Derivative of ReLU: 1 if hidden > 0, else 0
        grad_hidden[h] = (hidden[h] > 0) ? sum : 0;
      }

      // Now update weights
      // Update Layer 2
      for (int o = 0; o < OUTPUT_NODES; o++) {
        nn->b2[o] -= LEARNING_RATE * grad_output[o];
        for (int h = 0; h < HIDDEN_NODES; h++) {
          nn->w2[h][o] -= LEARNING_RATE * grad_output[o] * hidden[h];
        }
      }

      // Update Layer 1
      for (int h = 0; h < HIDDEN_NODES; h++) {
        nn->b1[h] -= LEARNING_RATE * grad_hidden[h];
        for (int in = 0; in < INPUT_NODES; in++) {
          nn->w1[in][h] -= LEARNING_RATE * grad_hidden[h] * input[in];
        }
      }
    }

    printf("Epoch %d: Loss = %.4f, Accuracy = %.2f%%\n", epoch + 1,
           total_loss / NUM_TRAIN, (float)correct * 100 / NUM_TRAIN);
  }
}

void save_model(NeuralNetwork *nn) {
  FILE *f = fopen("model.bin", "wb");
  if (!f) {
    printf("Error saving model.\n");
    return;
  }

  fwrite(nn->w1, sizeof(float), INPUT_NODES * HIDDEN_NODES, f);
  fwrite(nn->b1, sizeof(float), HIDDEN_NODES, f);
  fwrite(nn->w2, sizeof(float), HIDDEN_NODES * OUTPUT_NODES, f);
  fwrite(nn->b2, sizeof(float), OUTPUT_NODES, f);

  fclose(f);
  printf("Model saved to model.bin\n");
}

int main() {
  srand(time(NULL));

  load_mnist();

  NeuralNetwork nn;
  init_network(&nn);

  printf("Starting training...\n");
  train(&nn);

  save_model(&nn);

  free(train_images);
  free(train_labels);

  return 0;
}
