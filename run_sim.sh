pkill -f gzserver
pkill -f gzclient


source devel/setup.bash
roslaunch amclwithmap_pkg amcl_with_map.launch map_name:=my_map08202100 sim:=true model:=mini

