## 求解器
需要安装OSQP，OSQP-Eigen，如下安装。
- Download [osqp-v0.6.3-src.tar.gz](https://github.com/osqp/osqp/releases/tag/v0.6.3) or click [here](https://github.com/osqp/osqp/releases/download/v0.6.3/osqp-v0.6.3-src.tar.gz), and then follow the [installation instructions](https://osqp.org/docs/get_started/sources.html)
- Download [OSQP-eigen v0.8.1](https://github.com/robotology/osqp-eigen/releases/tag/v0.8.1).
```bash
cd osqp-eigen
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr/local ../
make
sudo make install
```
## 编译
```
catkin_make -DCMAKE_BUILD_TYPE=Release
```

## 一键启动仿真
```
bash run_sim.sh
```

## packages介绍

## 1 robot

`功能：` 启动仿真环境，会出现一个带雷达的小车。
source devel/setup.bash

`启动：`
```
roslaunch amclwithmap_pkg amcl_with_map.launch map_name:=my_map08202100 sim:=true model:=mini
```

实物中：1、编写了AMCL功能包、map_Server、handsfree_hw硬件驱动
2、更改fast_planner订阅的/odom
3、更改mpc_node订阅/发布的/odom /cmd_vel
4、注释了handsfree_hw包里的joint_publisher
## 2 lidar2world

`功能：` 把雷达系的点云转到odom系下，传给fastplanner的建图包。

`启动：`  
```
rosrun lidar2world lidar2world_node
```

## 3 mpc_tracking

`功能：` 跟踪fastplanner生成的局部轨迹

`启动：` 
```
rosrun mpc_tracking mpc_tracking_node
```

### 4 Fast-planner

`功能：` 实时建立小车前方摄像头可视范围内的障碍物地图，实时地生成避障轨迹，是一个位置环境的局部规划器。

详情请见：https://github.com/HKUST-Aerial-Robotics/Fast-Planner
```
roslaunch plan_manage 16_lidar.launch # 运行算法
roslaunch plan_manage rviz.launch # 可视化
```


