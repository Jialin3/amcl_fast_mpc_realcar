#include <ros/ros.h>
#include <geometry_msgs/Twist.h>
#include <nav_msgs/Odometry.h>
#include <nav_msgs/Path.h>
#include <geometry_msgs/PoseStamped.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>
#include "bspline/non_uniform_bspline.h"
#include "mpc_tracking/Bspline.h"
#include "std_msgs/Empty.h"

#include "mpc_tracking/mpc.h"

// #define BACKWARD_HAS_DW 1
// #include "backward.hpp"
// namespace backward{
//     backward::SignalHandling sh;
// }

using fast_planner::NonUniformBspline;
#define PI 3.1415926
ros::Publisher cmd_vel_pub, motion_path_pub, predict_path_pub;
nav_msgs::Path predict_path, motion_path;
nav_msgs::Odometry odom;

bool receive_traj = false;
vector<NonUniformBspline> traj;
double traj_duration;
ros::Time start_time;

ros::Timer control_cmd_pub, path_pub;

const int N = 10;
const double dt = 0.1;
Eigen::Vector3d current_state;

unique_ptr<Mpc> mpc_ptr;


void bsplineCallback(mpc_tracking::BsplineConstPtr msg) {
  // parse pos traj
  Eigen::MatrixXd pos_pts(msg->pos_pts.size(), 3);

  Eigen::VectorXd knots(msg->knots.size());
  for (int i = 0; i < msg->knots.size(); ++i) {
    knots(i) = msg->knots[i];
  }

  for (int i = 0; i < msg->pos_pts.size(); ++i) {
    pos_pts(i, 0) = msg->pos_pts[i].x;
    pos_pts(i, 1) = msg->pos_pts[i].y;
    pos_pts(i, 2) = msg->pos_pts[i].z;
  }

  NonUniformBspline pos_traj(pos_pts, msg->order, 0.1);
  pos_traj.setKnot(knots);

  // parse yaw traj

  Eigen::MatrixXd yaw_pts(msg->yaw_pts.size(), 1);
  for (int i = 0; i < msg->yaw_pts.size(); ++i) {
    yaw_pts(i, 0) = msg->yaw_pts[i];
  }

  NonUniformBspline yaw_traj(yaw_pts, msg->order, msg->yaw_dt);

  start_time = msg->start_time;

  traj.clear();
  traj.push_back(pos_traj);
  traj.push_back(traj[0].getDerivative());
  traj.push_back(traj[1].getDerivative());
  traj.push_back(yaw_traj);
  traj.push_back(yaw_traj.getDerivative());

  traj_duration = traj[0].getTimeSum();

  receive_traj = true;
}

void replanCallback(std_msgs::Empty msg) {
  /* reset duration */
  const double time_out = 0.01;
  ros::Time time_now = ros::Time::now();
  double t_stop = (time_now - start_time).toSec() + time_out;
  traj_duration = min(t_stop, traj_duration);
}

void odomCallback(const nav_msgs::Odometry &msg) {
    odom = msg;
    current_state(0) = msg.pose.pose.position.x;
    current_state(1) = msg.pose.pose.position.y;
    current_state(2) = tf2::getYaw(msg.pose.pose.orientation);

    //double yaw1 = tf2::getYaw(msg.pose.pose.orientation);
    // Eigen::Quaterniond quaternion;
    // quaternion.x() = msg.pose.pose.orientation.x;
    // quaternion.y() = msg.pose.pose.orientation.y;
    // quaternion.z() = msg.pose.pose.orientation.z;
    // quaternion.w() = msg.pose.pose.orientation.w;


    // Eigen::Matrix3d rotationMatrix = quaternion.toRotationMatrix();
    // double yaw2 = atan2(rotationMatrix(1, 0), rotationMatrix(0, 0));
    //cout << "x:" << current_state(0) << " " << "y:" << current_state(1) << endl;
    //cout << "yaw1:" << current_state(2) << endl;
    //cout << "yaw2:" << yaw2 << endl;

}

void publish_control_cmd(const ros::TimerEvent &e) {
    if (!receive_traj) return;
    ros::Time time_now = ros::Time::now();
    double t_cur = (time_now - start_time).toSec();
    Eigen::Vector3d pos, vel, acc, pos_f;
    double yaw, yawdot;
    Eigen::MatrixXd desired_state = Eigen::MatrixXd::Zero(N, 3);
    Eigen::MatrixXd ref_input = Eigen::MatrixXd::Zero(N, 2);
    desired_state(0, 2) = current_state(2);  //把小车当前yaw传入
    if (t_cur + (N-1) * dt <= traj_duration && t_cur > 0) {
      for (int i = 0; i < N; ++i) {
        pos = traj[0].evaluateDeBoorT(t_cur + i * dt);
        vel = traj[1].evaluateDeBoorT(t_cur + i * dt);
        acc = traj[2].evaluateDeBoorT(t_cur + i * dt);
        yaw = traj[3].evaluateDeBoorT(t_cur + i * dt)[0];
        yawdot = traj[4].evaluateDeBoorT(t_cur + i * dt)[0];

        if(i < N-1){
          Eigen::Vector3d vel_next = traj[1].evaluateDeBoorT(t_cur + (i+1) * dt);
          double yaw1 = desired_state(i, 2);
          double yaw2 = atan2(vel_next(1),vel_next(0));
        
          if(abs(yaw2-yaw1)>PI){
            if(yaw2<yaw1) {ref_input(i,1)=(2*PI+(yaw2-yaw1))/dt;yaw2 += 2*PI;}
            else {ref_input(i,1)=((yaw2-yaw1)-2*PI)/dt;yaw2 -= 2*PI;}
          }
          else ref_input(i,1) = (yaw2-yaw1)/dt;
          desired_state(i+1, 2) = yaw2;
        }
        
        desired_state(i, 0) = pos[0];
        desired_state(i, 1) = pos[1];
        ref_input(i,0) = vel.norm();
 //       ref_input(i,1) = yawdot;
      }

    } else if (t_cur + (N-1) * dt > traj_duration && t_cur < traj_duration) {
        int more_num = (t_cur + (N-1) * dt - traj_duration) / dt + 1;
        for (int i = 0; i < N - more_num; ++i) {
          pos = traj[0].evaluateDeBoorT(t_cur + i * dt);
          vel = traj[1].evaluateDeBoorT(t_cur + i * dt);
          acc = traj[2].evaluateDeBoorT(t_cur + i * dt);
          yaw = traj[3].evaluateDeBoorT(t_cur + i * dt)[0];
          yawdot = traj[4].evaluateDeBoorT(t_cur + i * dt)[0];

         if(i < N-1){
          Eigen::Vector3d vel_next = traj[1].evaluateDeBoorT(t_cur + (i+1) * dt);
          double yaw1 = desired_state(i, 2);
          double yaw2 = atan2(vel_next(1),vel_next(0));
        
          if(abs(yaw2-yaw1)>PI){
            if(yaw2<yaw1) {ref_input(i,1)=(2*PI+(yaw2-yaw1))/dt;yaw2 += 2*PI;}
            else {ref_input(i,1)=((yaw2-yaw1)-2*PI)/dt;yaw2 -= 2*PI;}
          }
          else ref_input(i,1) = (yaw2-yaw1)/dt;
          desired_state(i+1, 2) = yaw2;
        }

          desired_state(i, 0) = pos(0);
          desired_state(i, 1) = pos(1);
          ref_input(i,0) = vel.norm();
 //         ref_input(i,1) = yawdot;         
        }
        for (int i = N - more_num; i < N; ++i) {
          pos = traj[0].evaluateDeBoorT(traj_duration);
          vel.setZero();
          acc.setZero();
          yaw = traj[3].evaluateDeBoorT(traj_duration)[0];
          yawdot = traj[4].evaluateDeBoorT(traj_duration)[0];

          desired_state(i, 0) = pos(0);
          desired_state(i, 1) = pos(1);
          desired_state(i, 2) = yaw;
          ref_input(i,0) = 0;
          ref_input(i,1) = 0;
        }
    } else if (t_cur >= traj_duration)  {
      pos = traj[0].evaluateDeBoorT(traj_duration);
      vel.setZero();
      acc.setZero();
      yaw = traj[3].evaluateDeBoorT(traj_duration)[0];
      yawdot = traj[4].evaluateDeBoorT(traj_duration)[0];
      for (int i = 0; i < N; ++i) {
          desired_state(i, 0) = pos(0);
          desired_state(i, 1) = pos(1);
          desired_state(i, 2) = yaw;

          ref_input(i,0) = 0;
          ref_input(i,1) = 0;
      }
    } else {
      cout << "[Traj server]: invalid time." << endl;
  }
   // Eigen::MatrixXd desired_state1 = desired_state.transpose(); // 3 * (N + 1)
    //Eigen::Matrix2Xd desired_state2 = desired_state1.block(0, 0, 2, N + 1);
    //cout << "desired states matrix:" << desired_state2 << endl;
    Eigen::Vector3d x0;
    x0 << current_state(0), current_state(1), current_state(2);
    ros::Time solve_start = ros::Time::now();
    //mpc_ptr->solveMpc(x0, desired_state2);
    vector<Eigen::Vector3d> X_r(N);
    vector<Eigen::Vector2d> U_r(N); //最后一个参考输入不会被使用到的
    for (int i = 0; i < N; ++i) {
      if(i==0) X_r[i] << x0;
      else
      X_r[i] << desired_state(i, 0),desired_state(i, 1),desired_state(i, 2);
    //  X_r[i] << desired_state(i, 0),desired_state(i, 1),current_state(2);
      U_r[i] << ref_input(i,0), ref_input(i,1);
    }

    mpc_ptr->solveMpc(x0, X_r, U_r);
    cout << "yaw:" << x0 << endl;
    ros::Time solve_end = ros::Time::now();
    double t_solve = (solve_end - solve_start).toSec();
    cout << "solve time:" << t_solve << endl;
    geometry_msgs::Twist cmd;
    auto control_cmd = mpc_ptr->getControlCmd();
    //cout << "got cmd" << endl;
    cmd.linear.x = control_cmd(0);
    cmd.angular.z = control_cmd(1);
    //cout << "v:" << control_cmd(0) << " " << "w:" << control_cmd(1) << endl;
    cmd_vel_pub.publish(cmd);
    //cout << "u:" << result[0] << " " << "r:" << result[1] << endl;

    predict_path.header.frame_id = "odom";
    predict_path.header.stamp = ros::Time::now();
    geometry_msgs::PoseStamped pose_msg;
    geometry_msgs::Point pt;
   // ROS_INFO("00000000000000000000000000000000");
    Eigen::MatrixXd predict_states = mpc_ptr->getPredictState(x0, X_r, U_r);
    //cout << "got predict x" << endl;
    for (int i = 0; i < predict_states.rows(); i += 3) {
        pose_msg.pose.position.x = predict_states(i,0);
        pose_msg.pose.position.y = predict_states(i+1, 0);
        predict_path.poses.push_back(pose_msg);
    }
    predict_path_pub.publish(predict_path);
    
    predict_path.poses.clear();
}


int main(int argc, char **argv)
{
    ros::init(argc, argv, "mpc_tracking_node");
    ros::NodeHandle nh;
    mpc_ptr.reset(new Mpc());
///mobile_base/mobile_base_controller
    cmd_vel_pub = nh.advertise<geometry_msgs::Twist>("/mobile_base/mobile_base_controller/cmd_vel", 1);
    predict_path_pub = nh.advertise<nav_msgs::Path>("/predict_path", 1);
    motion_path_pub = nh.advertise<nav_msgs::Path>("/motion_path", 1);
   
    ros::Subscriber odom_sub = nh.subscribe("/mobile_base/mobile_base_controller/odom", 1, &odomCallback);
    ros::Subscriber bspline_sub = nh.subscribe("planning/bspline", 10, bsplineCallback);
    ros::Subscriber replan_sub = nh.subscribe("planning/replan", 10, replanCallback);

    control_cmd_pub = nh.createTimer(ros::Duration(0.05), publish_control_cmd);
    
    ros::spin();
    return 0;

}



