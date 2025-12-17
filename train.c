#include "model.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TRAIN_IMG_PATH "dataset/train-images.idx3-ubyte"
#define TRAIN_LBL_PATH "dataset/train-labels.idx1-ubyte"

#define LEARNING_RATE 0.1f
#define EPOCHS 15
#define BATCH_SIZE 32
#define NUM_TRAIN 60000

uint32_t flip_bytes(uint32_t val) {
  return ((val >> 24) & 0x000000FF) | ((val >> 8) & 0x0000FF00) |
         ((val << 8) & 0x00FF0000) | ((val << 24) & 0xFF000000);
}

uint8_t *train_images = NULL;
uint8_t *train_labels = NULL;

void load_mnist() {
  FILE *f_img = fopen(TRAIN_IMG_PATH, "rb");
  FILE *f_lbl = fopen(TRAIN_LBL_PATH, "rb");

  if (!f_img || !f_lbl) {
    printf("Error: Could not open dataset files. Check path: dataset/\n");
    exit(1);
  }

  uint32_t magic, num_items, rows, cols;

  fread(&magic, 4, 1, f_img);
  fread(&num_items, 4, 1, f_img);
  fread(&rows, 4, 1, f_img);
  fread(&cols, 4, 1, f_img);

  fread(&magic, 4, 1, f_lbl);
  fread(&num_items, 4, 1, f_lbl); // Should match

  num_items = flip_bytes(num_items);

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
      nn->w1[i][j] = rand_weight() * sqrtf(2.0f / INPUT_NODES); // He init

  for (int i = 0; i < HIDDEN_NODES; i++)
    nn->b1[i] = 0;

  for (int i = 0; i < HIDDEN_NODES; i++)
    for (int j = 0; j < OUTPUT_NODES; j++)
      nn->w2[i][j] = rand_weight() * sqrtf(2.0f / HIDDEN_NODES);

  for (int i = 0; i < OUTPUT_NODES; i++)
    nn->b2[i] = 0;
}

void augment_image(const uint8_t *src, float *dst) {
  int dx = (rand() % 5) - 2; // -2 to 2
  int dy = (rand() % 5) - 2; // -2 to 2

  if (rand() % 2 == 0) {
    dx = 0;
    dy = 0;
  }

  for (int r = 0; r < 28; r++) {
    for (int c = 0; c < 28; c++) {
      int sr = r - dy;
      int sc = c - dx;
      if (sr >= 0 && sr < 28 && sc >= 0 && sc < 28) {
        dst[r * 28 + c] = src[sr * 28 + sc] / 255.0f;
      } else {
        dst[r * 28 + c] = 0.0f;
      }
    }
  }
}

void train(NeuralNetwork *nn) {
  int *indices = malloc(NUM_TRAIN * sizeof(int));
  for (int i = 0; i < NUM_TRAIN; i++)
    indices[i] = i;

  float *inputs = malloc(BATCH_SIZE * INPUT_NODES * sizeof(float));
  int *targets = malloc(BATCH_SIZE * sizeof(int));

  static float dw1[INPUT_NODES][HIDDEN_NODES];
  static float db1[HIDDEN_NODES];
  static float dw2[HIDDEN_NODES][OUTPUT_NODES];
  static float db2[OUTPUT_NODES];

  for (int epoch = 0; epoch < EPOCHS; epoch++) {

    for (int i = NUM_TRAIN - 1; i > 0; i--) {
      int j = rand() % (i + 1);
      int temp = indices[i];
      indices[i] = indices[j];
      indices[j] = temp;
    }

    double total_loss = 0;
    int correct_total = 0;
    int batches = NUM_TRAIN / BATCH_SIZE;

    for (int b = 0; b < batches; b++) {

      memset(dw1, 0, sizeof(dw1));
      memset(db1, 0, sizeof(db1));
      memset(dw2, 0, sizeof(dw2));
      memset(db2, 0, sizeof(db2));

      int batch_correct = 0;
      double batch_loss = 0;

      for (int i = 0; i < BATCH_SIZE; i++) {
        int idx = indices[b * BATCH_SIZE + i];

        float input[INPUT_NODES];
        augment_image(&train_images[idx * 784], input);
        int target = train_labels[idx];

        float hidden[HIDDEN_NODES];
        float output[OUTPUT_NODES];

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
          output[o] = expf(output[o] - max_logit);
          sum_exp += output[o];
        }
        for (int o = 0; o < OUTPUT_NODES; o++)
          output[o] /= sum_exp;

        // Stats
        if (output[target] > 1e-9)
          batch_loss += -logf(output[target]);

        int pred = 0;
        float max_prob = output[0];
        for (int o = 1; o < 10; o++) {
          if (output[o] > max_prob) {
            max_prob = output[o];
            pred = o;
          }
        }
        if (pred == target)
          batch_correct++;

        // 2. Backprop - Accumulate Gradients
        float grad_output[OUTPUT_NODES];
        for (int o = 0; o < OUTPUT_NODES; o++) {
          grad_output[o] = output[o] - (o == target ? 1.0f : 0.0f);

          db2[o] += grad_output[o];
          for (int h = 0; h < HIDDEN_NODES; h++) {
            dw2[h][o] += grad_output[o] * hidden[h];
          }
        }

        // Backprop to Hidden
        float grad_hidden[HIDDEN_NODES];
        for (int h = 0; h < HIDDEN_NODES; h++) {
          float sum = 0;
          for (int o = 0; o < OUTPUT_NODES; o++) {
            sum += grad_output[o] * nn->w2[h][o];
          }
          grad_hidden[h] = (hidden[h] > 0) ? sum : 0; // ReLU deriv

          db1[h] += grad_hidden[h];
          for (int in = 0; in < INPUT_NODES; in++) {
            dw1[in][h] += grad_hidden[h] * input[in];
          }
        }
      }

      // Apply Gradients (Average over batch)
      float lr_batch = LEARNING_RATE / BATCH_SIZE;

      for (int o = 0; o < OUTPUT_NODES; o++) {
        nn->b2[o] -= lr_batch * db2[o];
        for (int h = 0; h < HIDDEN_NODES; h++) {
          nn->w2[h][o] -= lr_batch * dw2[h][o];
        }
      }
      for (int h = 0; h < HIDDEN_NODES; h++) {
        nn->b1[h] -= lr_batch * db1[h];
        for (int in = 0; in < INPUT_NODES; in++) {
          nn->w1[in][h] -= lr_batch * dw1[in][h];
        }
      }

      total_loss += batch_loss;
      correct_total += batch_correct;
    }

    printf("Epoch %d: Loss = %.4f, Accuracy = %.2f%%\n", epoch + 1,
           total_loss / NUM_TRAIN, (float)correct_total * 100 / NUM_TRAIN);

    // Decay learning rate
    // LEARNING_RATE *= 0.9f; // cannot assign to macro, but logic exists
  }

  free(indices);
  free(inputs);
  free(targets);
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

  printf("Starting training (Mini-batch Size: %d, Hidden Nodes: %d)...\n",
         BATCH_SIZE, HIDDEN_NODES);
  train(&nn);

  save_model(&nn);

  free(train_images);
  free(train_labels);

  return 0;
}
