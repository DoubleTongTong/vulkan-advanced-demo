#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/for_each.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

int main() {
    // Taskflow 是任务图：先把任务和依赖关系描述好，再交给 executor 执行。
    tf::Taskflow taskflow;

    // 这组数据没有特殊含义，只是用来观察并行任务的执行顺序。
    std::vector<int> items{1, 2, 3, 4, 5, 6, 7, 8};

    // for_each_index 会创建一个并行 for 任务。
    // 输出顺序可能每次都不一样，这正是并行调度的正常表现。
    auto printItems = taskflow
                          .for_each_index(
                              0u,
                              static_cast<unsigned>(items.size()),
                              1u,
                              [&](unsigned index) {
                                  std::cout << items[index];
                              })
                          .name("for_each_index");

    // Start 任务必须先于并行 for 执行。
    taskflow.emplace([] {
                std::cout << "\nS - Start\n";
            })
        .name("S")
        .precede(printItems);

    // End 任务必须等并行 for 完成后再执行。
    taskflow.emplace([] {
                std::cout << "\nT - End\n";
            })
        .name("T")
        .succeed(printItems);

    // 导出任务依赖图，后续可以用 GraphViz 转成图片观察结构。
    std::filesystem::create_directories("debug-output");
    std::ofstream graphFile("debug-output/taskflow.dot");
    taskflow.dump(graphFile);

    // executor 负责真正调度和执行任务图。
    tf::Executor executor;
    executor.run(taskflow).wait();

    return 0;
}
