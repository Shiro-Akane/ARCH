/**
 * @file Networks.h
 * @brief 统一网络聚合总线 (Static Duck Typing)
 * 不再需要虚基类和工厂，纯粹作为具体网络类型的引入点。
 */
#pragma once

// 引入底层所需的矩阵类型定义
#include "../../numerics/linalg/DenseWrap.h"
#include "../../numerics/linalg/SparseWrap.h"

// 聚合所有可用的网络模块
#include "NetPynucastro.h"

// 如果你使用的是 C++20，可以在这里写一个 Concept 来约束所有的 NetType 必须实现特定接口。
// 但在 C++11/14/17 下，由于模板的鸭子类型特性（不用提前声明，只要接口名字对得上就能编译），
// 这个文件只需要包含上述 #include 即可。