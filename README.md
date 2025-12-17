# Calculator

A high-performance, native calculator application developed in C. This project integrates standard computational features with an embedded machine learning engine for handwritten digit recognition, rendered using hardware-accelerated vector graphics.

## Architecture & Features

### Core Computational Modes
*   **Basic Arithmetic**: Standard algebraic operation processing with operator precedence support.
*   **Scientific Suite**: Advanced mathematical functions including trigonometry (`sin`, `cos`, `tan`), logarithmic operations (`log`, `ln`), and mathematical constants (`π`, `e`).
*   **RPN (Reverse Polish Notation)**: Stack-based calculation environment favored by engineers for complex evaluation without parentheses.
    *   Stack manipulation controls: `ENT` (Enter), `SWP` (Swap), `DRP` (Drop), `CLR` (Clear Stack).
*   **Unit Conversion**: dedicated interface for real-time conversion across Length, Mass, and Temperature units.

### Neural Input System
*   **Embedded Inference Engine**: Custom C implementation of a 2-layer Multilayer Perceptron (MLP) for recognizing handwritten digits.
*   **Architecture**: 784-input (28x28 normalized grid) → 128 hidden units → 10 output classes.
*   **Signal Processing Pipeline**: Raw input undergoes real-time bounding box extraction, center-of-mass alignment, and bilinear interpolation to match MNIST training distributions before inference.

### Systems Engineering
*   **Graphics Engine**: UI rendered via **NanoVG** over an **OpenGL** context for resolution-independent, antialiased vector graphics.
*   **Responsiveness**: Dynamic layout engine that automatically reflows button grids and font sizes based on window dimensions.
*   **Persistence**: Automatic serialization of calculation history and application state.

## Technical Requirements

*   **Compiler**: C99 compliant compiler (GCC/Clang)
*   **Dependencies**:
    *   SDL2 (Simple DirectMedia Layer)
    *   SDL2_ttf (TrueType Font support)
    *   OpenGL Framework (macOS) or GL libraries (Linux)

## Build Instructions

### 1. Training the Neural Network
The application requires a trained model binary for the input system. The training module reads standard MNIST dataset files.

```bash
# Compile the training utility
cc -O2 -o train train.c -lm

# Execute training (generates model.bin)
./train
```
*Ensure uncompressed MNIST dataset files are present in the `dataset/` directory.*

### 2. Building the Application
Link against SDL2 and the math library to generate the executable.

```bash
# Compile and link
cc -O2 -o calc main.c -lSDL2 -lSDL2_ttf -framework OpenGL -lm

# Launch application
./calc
```

## Project Layout

*   `main.c`: Application entry point, window management, UI rendering, and inference logic.
*   `train.c`: Standalone backpropagation implementation for model training.
*   `model.h`: Shared data structures for network topology and weighing.
*   `nanovg*`: Vector graphics library headers and implementation.
