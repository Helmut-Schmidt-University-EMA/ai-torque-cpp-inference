# Torque Sensor C++ Model Runner

This project demonstrates how to load and run a our AI models in C++ using LibTorch.

## 🔧 Setup and Running

### 0. Clone the repo

```sh
git clone git@github.com:Helmut-Schmidt-University-EMA/ai-torque-cpp-inference.git
```

### 1. Download LibTorch

Download the appropriate version of **LibTorch** (C++ distribution of PyTorch) from the [official website](https://pytorch.org/get-started/locally/) and extract it.

**Note** : Make sure target system is CPU based not GPU (at least for now)

### 2. Build the Project

Open a terminal and run:

### For Linux, Mac
```bash
cd ai-torque-cpp-inference
mkdir -p build
cd build
cmake ..
make
```

### For windows 
```bash
mkdir build
cd build
cmake -G Ninja ..
ninja
```

### 3- copy the model to build folder

download the model after training from the web platform and put in the build folder

### 4- run the model

```sh
./daqmodels_exec
```


## How to burn on DAQ 

1. Build on linux 
2. create compiled_model folder on daq with 3 files (`model.pt`, `daqmodels_exec`, `torchlibs`)
3. Download libtorch for cpu and then copy libtorch/lib .so files to `torchlibs` folder 
4. on daq device run this 
```sh
cd compiled_model
export  LD_LIBRARY_PATH=./torchlibs/:$LD_LIBRARY_PATH
./daqmodels_exec
```