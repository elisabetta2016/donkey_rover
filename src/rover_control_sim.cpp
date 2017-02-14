#include <ros/ros.h>
#include <tf/transform_broadcaster.h>
#include "libRover.h"
#include <stdlib.h>
#include <tf/transform_datatypes.h>
#include <math.h>
// Messages Standard
#include <geometry_msgs/Twist.h>
#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/Joy.h>
#include <sensor_msgs/Imu.h>
#include <std_msgs/Float32.h>
// Messages Custom
#include "donkey_rover/Scanner_Command.h"
#include "donkey_rover/Rover_Track_Speed.h"
#include "donkey_rover/Rover_Track_Bogie_Angle.h"
#include "donkey_rover/Rover_Scanner.h"
#include "donkey_rover/Rover_Power_Data.h"
#include "donkey_rover/Speed_control.h"
#include "sherpa_msgs/Attitude.h"


#include<iostream>

class Control_Sim
{
  public:
  Control_Sim(ros::NodeHandle& node)
  {

  }
  void handle(){

  }

protected:
  /*state here*/
  ros::NodeHandle n_;

  // Subscribers
  ros::Subscriber subFromAttitude_;
  ros::Subscriber subFromJoystick_;

private:

};
int main(int argc, char **argv)
{
  ros::init(argc, argv, "control_sim");
  ros::NodeHandle node;

  Control_Sim ctrl(node);

  ctrl.handle();

  return 0;
}
