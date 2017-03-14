#include <ros/ros.h>
#include <math.h>
#include <donkey_rover/Scanner_Command.h>
#include <donkey_rover/Rover_Scanner.h>

//Messages
 #include <sensor_msgs/LaserScan.h>

class DonkeyState
{
  public:

    DonkeyState(ros::NodeHandle& node)
    {
       n_=node;
       scan_pub = n_.advertise<sensor_msgs::LaserScan>("scan_hector", 5);
       scanner_ctrl_pub = n_.advertise<donkey_rover::Scanner_Command>("scanner_commands", 1);

       subFromScan = n_.subscribe("/scan",5,&DonkeyState::scan_cb,this);
       subFromScannerMsg = n_.subscribe("RoverScannerInfo",1,&DonkeyState::scanInfo_cb,this);

       ros::param::set("rover_state/scanner_state","horizontal");
       ros::param::set("rover_state/scanner_config/Period",3.0);
       ros::param::set("rover_state/scanner_config/Roll_angle",M_PI/2);
       ros::param::set("rover_state/scanner_config/Home_angle",0.0);

    }

    void scan_cb(const sensor_msgs::LaserScan::ConstPtr& scan)
    {
       std::string scanner_state;
       if(!ros::param::get("rover_state/scanner_state",scanner_state))
       {
         ROS_ERROR("parameter rover_state/scanner_state has been removed probably");
         return;
       }
       if (scanner_state.compare("horizontal")==0)
          scan_pub.publish(*scan);
    }

    void scanInfo_cb(const donkey_rover::Rover_Scanner::ConstPtr& msg)
    {
      if(msg->Scanner_angle < 0.001 && msg->Scanner_angle > -0.001)
        ros::param::set("rover_state/scanner_state","horizontal");
      else if(msg->Scanner_State.compare("Rolling") == 0)
       ros::param::set("rover_state/scanner_state","rolling");
      else
       ros::param::set("rover_state/scanner_state","unknownStill");
    }

    void Scanner_control()
    {
      std::string scanner_command;
      if(!ros::param::get("rover_state/scanner_command",scanner_command)) return;
      if (scanner_command.compare("NoCommand") == 0 ) return;
      if (scanner_command.compare("Roll") == 0 )
      {
        donkey_rover::Scanner_Command cmd;
        cmd.Scanner_Command = "Start";
        if(!ros::param::get("rover_state/scanner_config/Period",cmd.Scanner_Period)) cmd.Scanner_Period = -100.0;
        if(!ros::param::get("rover_state/scanner_config/Roll_angle",cmd.Scanner_Roll_Angle)) cmd.Scanner_Roll_Angle = -100.0;
        if(!ros::param::get("rover_state/scanner_config/Home_angle",cmd.Scanner_Home_Angle)) cmd.Scanner_Home_Angle = -100.0;
        scanner_ctrl_pub.publish(cmd);
        ros::param::set("rover_state/scanner_command","NoCommand");
        return;
      }
      if (scanner_command.compare("Home") == 0 )
      {
        donkey_rover::Scanner_Command cmd;
        cmd.Scanner_Command = "GoHome";
        if(!ros::param::get("rover_state/scanner_config/Period",cmd.Scanner_Period)) cmd.Scanner_Period = -100.0;
        if(!ros::param::get("rover_state/scanner_config/Roll_angle",cmd.Scanner_Roll_Angle)) cmd.Scanner_Roll_Angle = -100.0;
        if(!ros::param::get("rover_state/scanner_config/Home_angle",cmd.Scanner_Home_Angle)) cmd.Scanner_Home_Angle = -100.0;
        scanner_ctrl_pub.publish(cmd);
        ros::param::set("rover_state/scanner_command","NoCommand");
        return;
      }
    }

    void handle()
    {
       rate = 50.0;
       ros::Rate r(rate);
       while(n_.ok())
       {

         Scanner_control();
         r.sleep();
         ros::spinOnce();
       }

    }

  protected:
  /*state here*/
  ros::NodeHandle n_;

  // Subscribers
  ros::Subscriber subFromScan;
  ros::Subscriber subFromScannerMsg;

  // Publishers
  ros::Publisher scan_pub;
  ros::Publisher scanner_ctrl_pub;
  double rate;
};

int main(int argc, char **argv)
{
  ros::init(argc, argv, "DonkeyState");
  ros::NodeHandle node;

  DonkeyState donkeystate(node);
  donkeystate.handle();
  return 0;
}
