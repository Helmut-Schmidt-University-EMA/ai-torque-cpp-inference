# Torque Sensor C++ Model Runner

This project demonstrates how to load and run a our AI models in C++ using LibTorch.

## 🔧 Setup and Running

### 0. Clone the repo

```sh
git clone git@github.com:Helmut-Schmidt-University-EMA/ai-torque-cpp-inference.git
```

### 1. Download LibTorch

Download the appropriate version of **LibTorch** (C++ distribution of PyTorch) from the [official website](https://pytorch.org/get-started/locally/) and extract it.

### 2. Build the Project

Open a terminal and run:

```bash
cd daqmodels
mkdir -p build
cd build
cmake ..
make
```

### 3- copy the model to build folder

download the model after training from the web platform and put in the build folder

### 4- run the model

```sh
./daqmodels_exec
```
