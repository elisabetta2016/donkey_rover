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
#include <std_msgs/String.h>
// Messages Custom
#include "donkey_rover/Scanner_Command.h"
#include "donkey_rover/Rover_Track_Speed.h"
#include "donkey_rover/Rover_Track_Bogie_Angle.h"
#include "donkey_rover/Rover_Scanner.h"
#include "donkey_rover/Rover_Power_Data.h" 
#include "donkey_rover/Speed_control.h"
#include "sherpa_msgs/Attitude.h"

#include<iostream>

Rover rover(false);
EScannerState state;
float scannerRaw;
float scannerCal;
float VX = 0.0;
float VY = 0.0;
bool Low_Battery = false;
bool debug_ = false;

class DonkeyRoverClass
{
	public:
		
		DonkeyRoverClass(ros::NodeHandle& node)
		{
			n_=node;

			//subscribers
			subFromJoystick_ 		= n_.subscribe("joy", 1, &DonkeyRoverClass::joyCallback,this);
			subFromCMDVEL_ 			= n_.subscribe("cmd_vel", 1, &DonkeyRoverClass::CMDVELLCommander,this);
			subFromScannerCommander_ 	= n_.subscribe("c1", 10, &DonkeyRoverClass::ScannerCommander,this);
			subFromIMU_			= n_.subscribe("imu/data", 1, &DonkeyRoverClass::ImuSet,this);
			subFromRightLeftCommands_	= n_.subscribe("speedfollow", 5, &DonkeyRoverClass::RLcommander,this);
			subFromscannerdata_		= n_.subscribe("scanner_data", 1, &DonkeyRoverClass::scannerRawValueSet,this);
			subFromscannercommands_		= n_.subscribe("scanner_commands", 1, &DonkeyRoverClass::SetScanner,this);
			subFromAttitude_		= n_.subscribe("attitude", 1, &DonkeyRoverClass::setAttitude,this);
			subFromSpeedcontrol_	= n_.subscribe("speed_control",1,&DonkeyRoverClass::setSpeedControl,this);
//      subFromHazardmsg_      = n_.subscribe("Hazard",1,&DonkeyRoverClass::hazard_cb,this);
			// publishers
			odom_pub 	   	  = n_.advertise<nav_msgs::Odometry>("odom", 100);
			pose_pub		  = n_.advertise<geometry_msgs::PoseWithCovarianceStamped>("donkey_pose", 100);
			twist_pub 	  	  = n_.advertise<geometry_msgs::Twist>("twist", 100);
			Rover_Track_Speed_pub     = n_.advertise<donkey_rover::Rover_Track_Speed>("RoverTrackSpeed", 100);
			Rover_Track_Angles_pub    = n_.advertise<donkey_rover::Rover_Track_Bogie_Angle>("RoverTrackAngles", 100);	
			Rover_Scanner_Data_pub    = n_.advertise<donkey_rover::Rover_Scanner>("RoverScannerInfo", 100);
			Rover_Power_Data_pub      = n_.advertise<donkey_rover::Rover_Power_Data>("RoverPowerInfo", 100);
			Scanner_debug_pub	  = n_.advertise<geometry_msgs::Vector3>("Scanner_Debug", 100);
			Scanner_angle_pub	  = n_.advertise<std_msgs::Float32>("Scanner_angle_sync", 100);
			
			//Initializers
			delta_scanner = 0.0;
			last_scanner_value = 0.0;
			scanner_offsetVal = 0.0;
			scanner_hor_corrector = 0.0;

			Speed_control.CMD = false;
			Speed_control.JOY = true;
			Speed_control.RLC = false;
      Track_eneabled = true;

		}

//    void hazard_cb(const std_msgs::String::ConstPtr msg)
//    {
//         if (msg->data.compare("Red")==0)
//         {
//           ROS_ERROR("Donkey_Rover: Hazardous distance to obstacle, Stoping the Rover");
//           ROS_ERROR_COND(rover.disableTracks()!=0,"Failed to Disable Track");
//           Track_eneabled = false;
//         }
//         if(!Track_eneabled)
//           if(msg->data.compare("Red")!= 0)
//           {
//             Track_eneabled = true;
//             ROS_ERROR_COND(rover.enableTracks()!=0,"Failed to ReEnabe Tracks");
//           }
//    }
		void setSpeedControl(const donkey_rover::Speed_control::ConstPtr& msg)
		{
			Speed_control = *msg;
			Speed_control_sq = msg->header.seq; 
		}

  		void ImuSet(const sensor_msgs::Imu::ConstPtr& msg){
  			
  			Gyro_velocity = msg->angular_velocity;
  			if (madgwick_)
  				Quat_attitude = msg->orientation;
  		
  		}

		void shutdown()
		{
			int retCode = rover.sendScannerCommand(ESCDoHoming);
			ROS_WARN("CIAO!");
			Time::sleep(2, 0);
			//int o = system("sudo ~/catkin_ws/src/donkey_rover/scripts/./shutdown.sh");
			system("sudo poweroff");
			//system("roslaunch donkey_rover donkey_hector.launch");
		}
		
		void setAttitude(const sherpa_msgs::Attitude::ConstPtr& msg){
			euler_attitude = msg->euler;
			euler_attitude.z = euler_attitude.z; //+ M_PI/2;
			if (!madgwick_)
			{
			    	Quat_attitude = msg->quaternion;
			    	ROS_INFO_ONCE("Grande Nicola!!");
			}
		}

		void RLcommander(const geometry_msgs::Vector3::ConstPtr& s)
		{
  			geometry_msgs::Vector3 speed = *s;
  			float VL = speed.y;
  			float VR = speed.x;
			if(!Speed_control.RLC)
			{
        ROS_ERROR_THROTTLE(4,"RLC speed will not be executed due to false vale of speed_control/RLC");
			}

  			if(!Joystick && Speed_control.RLC){
  				int retCode = rover.setSpeed (10*VL,10*VR);
  				if (retCode != 0)
    			 		{
     			  	 // Failed to set the speed
     			   	ROS_ERROR("Failed to move the rover to follow");
  			 	}
  			}
		}
		
		void scannerRawValueSet(const geometry_msgs::Vector3::ConstPtr& vector)
		{
				scannerRaw = vector->x;
				
        	
        			// Start Scanner Angle Loop 
    				delta_scanner =scannerRaw - last_scanner_value;
				if (delta_scanner > 10000) { 
					scanner_offsetVal = scanner_offsetVal - 16383;
					//ROS_INFO("Scanner offsetVal is %f, scannerRaw is %f", scanner_offsetVal, scannerRaw);
					}
				else if(delta_scanner < -10000){  
					scanner_offsetVal = scanner_offsetVal + 16383;
					//ROS_WARN("Scanner offsetVal is %f, scannerRaw is %f", scanner_offsetVal, scannerRaw);
					}
				last_scanner_value = scannerRaw;

				
				scannerCal = scannerRaw + scanner_offsetVal + scanner_hor_corrector;
				std_msgs::Float32 angle_msgs;
				angle_msgs.data = scannerCal/244.6*M_PI/180;
				Scanner_angle_pub.publish(angle_msgs);
				
    				tf::Quaternion scan_quart,scan_quart_back;
    				scan_quart.setRPY     ((( scannerCal/244.6)*M_PI/180)+M_PI,0.0 , 0.0);// (scannerCal/23713)*M_PI/2)
                    		scan_quart_back.setRPY((-(scannerCal/244.6)*M_PI/180)+M_PI,0.0 , 0.0);// (scannerCal/23713)*M_PI/2)
    				broadcaster1.sendTransform(
      				tf::StampedTransform(

        			tf::Transform(scan_quart, tf::Vector3(0.0, 0.0, 0.0)), ros::Time::now(),"laser","base_laser")
    				);
    				broadcaster2.sendTransform(
      				tf::StampedTransform(

        			tf::Transform(scan_quart_back, tf::Vector3(0.0, 0.0, 0.0)), ros::Time::now(),"base_laser","base_scanner")
    				);


		}
		
		void SetScanner(const donkey_rover::Scanner_Command::ConstPtr& msg)
		{
		donkey_rover::Scanner_Command scanner_command_msg = *msg;
		bool new_adjustment_angle;
		bool new_roll_angle;
		bool new_home_angle;
		bool new_scanner_period;
		bool new_scanner_command;
		
		if (abs(temp_adjustment_angle - scanner_command_msg.Scanner_Ajustment_Angle)<0.001) new_adjustment_angle=false;
		else {  new_adjustment_angle=true;
                if(debug_) ROS_INFO("New Adjustment Angle");}
		if (abs(temp_roll_angle - scanner_command_msg.Scanner_Roll_Angle)<0.001) new_roll_angle=false;
		else {  new_roll_angle = true; 
                new_roll_angle_2 = true;
                if(debug_) ROS_INFO("New roll Angle");}
		if (abs(temp_home_angle - scanner_command_msg.Scanner_Home_Angle)<0.001) new_home_angle=false;
		else {  new_home_angle=true;
                if(debug_) ROS_INFO("New home Angle");}
		if (abs(temp_scanner_period - scanner_command_msg.Scanner_Period)<0.001) new_scanner_period=false;
		else {  new_scanner_period=true;
                if(debug_) ROS_INFO("New Scanner Period");}
		if (temp_scanner_command == scanner_command_msg.Scanner_Command) new_scanner_command=false;
		else {  new_scanner_command=true;
                if(debug_) ROS_INFO("New Scanner Command");}
		

		int retCode;
		if (!first_cycle){

  			
			//Scanner Adjustment Angle
			if (scanner_command_msg.Scanner_Ajustment_Angle != -100 && new_adjustment_angle){
				  temp_adjustment_angle = scanner_command_msg.Scanner_Ajustment_Angle;
				  ROS_INFO("Start Set Scanner Adjustment Angle Process");
    				  while (state != ESSIdle)
     			 	  {
       					cout << '.' << flush;
       					Time::sleep(1, 0);
       					retCode = rover.getScannerState(state);
     				  }
     			 	  retCode = rover.setScannerAdjustment(temp_adjustment_angle);
      			 	  if (retCode != 0) ROS_ERROR("Set Scanner Adjustment Angle failed");
				  else ROS_INFO("Scanner Ajustment Angle is successfully set to %f",temp_adjustment_angle);	
			}
			//Scanner Roll Angle
			if (scanner_command_msg.Scanner_Roll_Angle != -100 && new_roll_angle){
				  temp_roll_angle = scanner_command_msg.Scanner_Roll_Angle;
				  ROS_INFO("Start Set Scanner Roll Angle Process");
    				  while (state != ESSIdle)
     			 	  {
       					cout << '.' << flush;
       					Time::sleep(1, 0);
       					retCode = rover.getScannerState(state);
     				  }
     			 	  retCode = rover.setScannerAngle(temp_roll_angle);
      			 	  if (retCode != 0) ROS_ERROR("Set Scanner Roll Angle failed");
				  else ROS_INFO("Scanner Roll Angle is successfully set to %f",temp_roll_angle);	
			}
			//Scanner Home Angle
			if (scanner_command_msg.Scanner_Home_Angle  != -100  && new_home_angle){
				  temp_home_angle = scanner_command_msg.Scanner_Home_Angle;
				  ROS_INFO("Start Set Scanner Home Angle Process");
    				  while (state != ESSIdle)
     			 	  {
       					cout << '.' << flush;
       					Time::sleep(1, 0);
       					retCode = rover.getScannerState(state);
     				  }
     			 	  //retCode = rover.setScannerHomePosition(temp_home_angle);
                      		  ROS_ERROR("Do Not change the home position, use the adjustment angle instead");
      			 	  if (retCode != 0) ROS_ERROR("Set Scanner Home Angle failed");
				  else ROS_INFO("Scanner Home Angle is successfully set to %f",temp_home_angle);	
			}
			//Scanner Period 
			if (scanner_command_msg.Scanner_Period  != -100 && new_scanner_period){
				  temp_scanner_period = scanner_command_msg.Scanner_Period;
				  ROS_INFO("Start Set Scanner Period Process");
    				  while (state != ESSIdle)
     			 	  {
       					cout << '.' << flush;
       					Time::sleep(1, 0);
       					retCode = rover.getScannerState(state);
     				  }
     			 	  retCode = rover.setScannerPeriod(temp_scanner_period);
      			 	  if (retCode != 0) ROS_ERROR("Set Scanner Home Angle failed");
				  else ROS_INFO("Scanner Period is successfully set to %f",temp_scanner_period);	
			}
      //Commands
      temp_scanner_command = scanner_command_msg.Scanner_Command;
       if (scanner_command_msg.Scanner_Command =="GoHome" && new_scanner_command ){

              while (state != ESSIdle)
              {
                cout << '.' << flush;
                Time::sleep(1, 0);
                retCode = rover.getScannerState(state);
              }
                      retCode = rover.sendScannerCommand(ESCStop);
                        Time::sleep(1, 0);
              retCode = rover.sendScannerCommand(ESCGoHome);
                if (retCode != 0)
                  {
                       ROS_ERROR("Scanner GoHome failed");

                }
                scanner_horizontal = true;
         } else if(scanner_command_msg.Scanner_Command == "Start" && new_scanner_command)
         {
         ros::Duration(0.1).sleep();
            while (state != ESSIdle)
              {
                cout << '.' << flush;
                Time::sleep(1, 0);
                retCode = rover.getScannerState(state);
              }
              scannerRaw_init = scannerRaw;
              retCode = rover.sendScannerCommand(ESCStart);
              if (retCode != 0)
              {
                      ROS_ERROR("Scanner Starting failed");

             }
        } else if(scanner_command_msg.Scanner_Command == "Stop" && new_scanner_command)
        {
              retCode = rover.sendScannerCommand(ESCStop);
              if (retCode != 0)
              {
                ROS_ERROR("Scanner Stopping failed");

              }
        } else if (scanner_command_msg.Scanner_Command == "DoHoming" && new_scanner_command)
        {
            retCode = rover.sendScannerCommand(ESCDoHoming);

        }
        //else ROS_ERROR("Scanner Command is not valid");
		}
		}

		void ScannerCommander(const geometry_msgs::Vector3::ConstPtr& vector)
		{
 			 geometry_msgs::Vector3 new_correction = *vector;
  			 float msg = new_correction.z;
  			 int retCode;
			 /*
			 retCode = rover.setScannerHomePosition(0);
  			 if (retCode != 0)
    			 {
     			          ROS_ERROR("Failed TO Set Scanner Home Position");
  			 }*/
             		 //retCode = rover.getScannerState(state);
 			 if (msg ==1 ){
    				  while (state != ESSIdle)
     			 	  {
       					cout << '.' << flush;
       					Time::sleep(1, 0);
       					retCode = rover.getScannerState(state);
     				  }
     			 	  retCode = rover.sendScannerCommand(ESCDoHoming);
      			 	  if (retCode != 0)
      		 	  	  {
        		  	       printError("GoHome failed", retCode);

      				  }
  			 } else if(msg == 2)
  			 {
     			 	while (state != ESSIdle)
      				{
       					cout << '.' << flush;
       					Time::sleep(1, 0);
       					retCode = rover.getScannerState(state);
      			 	}
      			 	retCode = rover.sendScannerCommand(ESCStart);
      			 	if (retCode != 0)
      			 	{
       		         		printError("Starting failed", retCode);

     				 }
  			} else if(msg == 3)
  			{
      				retCode = rover.sendScannerCommand(ESCStop);
      				if (retCode != 0)
      				{
       					printError("Stopping failed", retCode);

      				}
  			} else if (msg == 4)
  			{
     				retCode = rover.sendScannerCommand(ESCDoHoming);

  			}else
  			{
      			//printf(" \n Command is not recognized, recognized commands are GoHome, Start and Stop : \n");

  			}

		}


		void CMDVELLCommander(const geometry_msgs::Twist::ConstPtr& vel)
		{
  			geometry_msgs::Twist new_vel = *vel;
  			float v = sqrt (new_vel.linear.x * new_vel.linear.x + new_vel.linear.y * new_vel.linear.y);
  			float vth = new_vel.angular.z;
			if(!Speed_control.CMD)
			{
				ROS_ERROR("CMD speed will not be executed due to false vale of speed_control/CMD");
			}
  			if(!Joystick && Speed_control.CMD){
  				int retCode = rover.setSpeedVO (v, vth);
    				if (retCode != 0)
       				 {
        			// Failed to set the speed
        			ROS_ERROR("Failed to set speed of the rover");
 		 		}
 		 	}

		}

		void joyCallback(const sensor_msgs::JoyConstPtr& joy)
		{
			int retCode;
  			float v=joy->axes[1];
  			float vth=joy->axes[2];
 		    	float a1=joy->buttons[4];
			//ROS_INFO("JOY: [1] = %f [2] = %f [4] = %f", v, vth, a1);
  			if (joy->buttons[6]==1 || joy->buttons[7]==1) Joystick = true;
  			if (joy->buttons[6]==0 && joy->buttons[7]==0){
  				Joystick = false;
  				v = 0;
  				vth = 0;
  				retCode = rover.setSpeedVO (v, vth);
  			}
  			if(Joystick){

  				v= v*(a1+1)/2;
				if (v > 0.8) v = 0.8;
				//ROS_INFO("JOY: v = %f v_th = %f", v, vth, a1);
  				retCode = rover.setSpeedVO (v, vth);
    				if (retCode != 0)
				{
     					// Failed to set the speed
					ROS_ERROR("Failed to set speed of the rover");
  				}
  				
  			}
			if(joy->buttons[0] && joy->buttons[1] && joy->buttons[4] && joy->buttons[5])
			{
				ROS_FATAL("Power off Request by user");
				shutdown();
			}
  			
  
		}

		// Class Functions
		void Scanner_Handle()
		{
			float syncf = 100.0;
    			
    			int retCode = rover.setScannerPeriod(2.00);
    			retCode = rover.setScannerAdjustment(0.00000000);
    			retCode = rover.getScannerState(state);
    			while (state != ESSIdle)
    				{
        			cout << '.' << flush;
        			Time::sleep(1, 0);
        			retCode = rover.getScannerState(state);
   			 }

  			retCode = rover.setSyncFreq(syncf);
  			if (retCode != 0)
        			{
        			// Failed to set sync frequency
        			printf("Failed to set sync frequency to %f",syncf);
  			}
            		retCode = rover.getScannerAngle(roll_angle);
  			retCode = rover.setScannerPeriod(2);
  			if (retCode != 0)
  			{
      				ROS_ERROR("setScannerPeriod failed");
  			}

		}

		void Rover_Handle()
		{
  			int retCode = rover.init();
  			if (retCode != 0)
			{
				ROS_ERROR("Failed Initialize the Rover");
  			}
		}
		
		/*void initParams()
		{
		std::string send_odom;
   		n.param<std::string>("send_odom", send_odom, "false");
   		if (send_odom == "true") {send_odom_=true; ROS_INFO("Send odom true");}
   		else {send_odom_=false;ROS_INFO("Send odom false");}
    		//if (n_.getparam)

		}*/
		
		void RoverDataProvider()
		{
			Time timestamp;
			float temp_Front_Left_Track_Speed;
			float temp_Front_Right_Track_Speed;
			float temp_Rear_Left_Track_Speed;
			float temp_Rear_Right_Track_Speed;

			float temp_Front_Left_Track_Angle;
			float temp_Front_Right_Track_Angle;
			float temp_Rear_Left_Track_Angle;
			float temp_Rear_Right_Track_Angle;
			float temp_Rear_Bogie_Angle;

			float temp_Scanner_Period;
			float temp_Scanner_adjustment_angle;
			float temp_Scanner_roll_angle;
			
			float temp_Battery_Voltage;
			float temp_Front_Right_Track_Current;
			float temp_Front_Left_Track_Current;
			float temp_Rear_Right_Track_Current;
			float temp_Rear_Left_Track_Current;


			
			
			int retCode; 
  			ros::Time current_time;
  			current_time = ros::Time::now();
			retCode = rover.setAngleReference(EDIFrontRightTrack,1.34);
			retCode = rover.setAngleReference(EDIFrontLeftTrack,-1.34);
			retCode = rover.setAngleReference(EDIRearRightTrack,1.34);
			retCode = rover.setAngleReference(EDIRearLeftTrack,-1.34);
				
			retCode = rover.getSpeedInMPerS(timestamp,
				  temp_Front_Left_Track_Speed, temp_Front_Right_Track_Speed,
				  temp_Rear_Left_Track_Speed,  temp_Rear_Right_Track_Speed  );
  			if (retCode != 0)
			{
				ROS_ERROR("Failed to Read Speed of Tracks, Try to Restart the Rover");
  			}
			retCode = rover.getAngles(timestamp,
				  temp_Front_Left_Track_Angle, temp_Front_Right_Track_Angle,
				  temp_Rear_Left_Track_Angle,  temp_Rear_Right_Track_Angle,
				  temp_Rear_Bogie_Angle);
  			if (retCode != 0)
			{
				ROS_ERROR("Failed to Read Angle of Tracks, Try to Restart the Rover");
  			}
			////////////////SCANNER STATE////////////////////
			EScannerState state2;	
			retCode = rover.getScannerState(state2);
			if (retCode != 0)
			{
				ROS_ERROR("Failed to Read Scanner State, Try to Restart the Rover");
  			}
			if(state2 == ESSUnknown) 	 outputScanner.Scanner_State = "Unknown!";
			if(state2 == ESSIdle)   		 outputScanner.Scanner_State = "Idle";
			if(state2 == ESSCommandReceived ) outputScanner.Scanner_State = "CommandReceived";
			if(state2 == ESSHoming) 		 outputScanner.Scanner_State = "Homing";
			if(state2 == ESSGoingHome) 	 outputScanner.Scanner_State = "GoingHome";
			if(state2 == ESSRolling) 	 outputScanner.Scanner_State = "Rolling";

			retCode = rover.getScannerPeriod(temp_Scanner_Period);
			if (retCode != 0)
			{
				ROS_ERROR("Failed to Read Scanner Period, Try to Restart the Rover");
  			}
			retCode = rover.getScannerAdjustment(temp_Scanner_adjustment_angle);
			if (retCode != 0)
			{
				ROS_ERROR("Failed to Read Scanner Adjustment angle, Try to Restart the Rover");
  			}
			retCode = rover.getScannerAngle(temp_Scanner_roll_angle);
            		roll_angle = temp_Scanner_roll_angle;
			if (retCode != 0)
			{
				ROS_ERROR("Failed to Read Scanner Roll angle, Try to Restart the Rover");
  			}


			retCode = rover.readVoltage(EDIFrontRightTrack,timestamp,temp_Battery_Voltage);
			if (retCode != 0)
			{
				ROS_ERROR("Failed to Read Battery Voltage, Try to Restart the Rover");
  			}
			retCode = rover.getCurrent(EDIFrontRightTrack,timestamp,temp_Front_Right_Track_Current);
			if (retCode != 0)
			{
				ROS_ERROR("Failed to Read Battery Voltage, Try to Restart the Rover");
  			}
			retCode = rover.getCurrent(EDIFrontRightTrack,timestamp,temp_Front_Right_Track_Current);
			if (retCode != 0)
			{
				ROS_ERROR("Failed to Read Front_Right_Track_Current, Try to Restart the Rover");
  			}
			retCode = rover.getCurrent(EDIFrontLeftTrack,timestamp,temp_Front_Left_Track_Current);
			if (retCode != 0)
			{
				ROS_ERROR("Failed to Read Front_Left_Track_Current, Try to Restart the Rover");
  			}
			retCode = rover.getCurrent(EDIRearRightTrack,timestamp,temp_Rear_Right_Track_Current);
			if (retCode != 0)
			{
				ROS_ERROR("Failed to Read Rear_Right_Track_Current, Try to Restart the Rover");
  			}
			retCode = rover.getCurrent(EDIRearLeftTrack,timestamp,temp_Rear_Left_Track_Current);
			if (retCode != 0)
			{
				ROS_ERROR("Failed to Read Rear_Left_Track_Current, Try to Restart the Rover");
  			}			


			outputTrackSpeed.Front_Left_Track_Speed	  = temp_Front_Left_Track_Speed;
			outputTrackSpeed.Front_Right_Track_Speed  = temp_Front_Right_Track_Speed;
			outputTrackSpeed.Rear_Left_Track_Speed    = temp_Rear_Left_Track_Speed;
			outputTrackSpeed.Rear_Right_Track_Speed   = temp_Rear_Right_Track_Speed;
			outputTrackSpeed.header.stamp = current_time;
   	 		outputTrackSpeed.header.frame_id = "base_link";
			outputTrackSpeed.TimeStamp = current_time.toSec();

			outputBogieAngle.Front_Left_Track_Angle   = temp_Front_Left_Track_Angle;
			outputBogieAngle.Front_Right_Track_Angle  = temp_Front_Right_Track_Angle;
			outputBogieAngle.Rear_Left_Track_Angle    = temp_Rear_Left_Track_Angle;
			outputBogieAngle.Rear_Right_Track_Angle   = temp_Rear_Right_Track_Angle;
			outputBogieAngle.Rear_Bogie_Angle         = temp_Rear_Bogie_Angle -1.31;
 			outputBogieAngle.header.stamp = current_time;
   	 		outputBogieAngle.header.frame_id = "base_link";
			outputBogieAngle.TimeStamp = current_time.toSec();
			
			outputScanner.Scanner_Period = temp_Scanner_Period;
			outputScanner.Scanner_adjustment_angle = temp_Scanner_adjustment_angle;
			outputScanner.Scanner_roll_angle = temp_Scanner_roll_angle;
			outputScanner.Scanner_angle = scannerCal;
			outputScanner.Scanner_angle_encoder = scannerCal;
			outputScanner.Scanner_angle_degree = scannerCal/244.6;
			outputScanner.Scanner_angle = (scannerCal/244.6)*M_PI/180;
 			outputScanner.header.stamp = current_time;
   	 		outputScanner.header.frame_id = "base_link";
			outputScanner.TimeStamp = current_time.toSec();

			outputPower.Battery_Voltage = temp_Battery_Voltage;
			outputPower.Front_Right_Track_Current = temp_Front_Right_Track_Current;
			outputPower.Front_Left_Track_Current  = temp_Front_Left_Track_Current;
			outputPower.Rear_Right_Track_Current  = temp_Rear_Right_Track_Current;
			outputPower.Rear_Left_Track_Current   = temp_Rear_Left_Track_Current;
 			outputPower.header.stamp = current_time;
   	 		outputPower.header.frame_id = "base_link";
			outputPower.TimeStamp = current_time.toSec();
			if(temp_Battery_Voltage < 46.00) Low_Battery = true;
			else Low_Battery = false;

      
			Rover_Track_Speed_pub.publish(outputTrackSpeed); 
			Rover_Track_Angles_pub.publish(outputBogieAngle);
			Rover_Scanner_Data_pub.publish(outputScanner);
			Rover_Power_Data_pub.publish(outputPower);

		}

        void Odometry_Handle()
		{
		    // Handling the parameters
  			ros::NodeHandle n("~");
   			n.param("send_odom", send_odom_, false);
   			n.param("odom_3D", odom_3D_, false);
   			n.param("madgwick", madgwick_, true);
		    n.param("x_front",x_front_config,false);
			n.param("debug",debug_,false);
			if(odom_3D_ && x_front_config)
			{
				ROS_ERROR("odom 3D is not compatible with x_front configuration, it is then set to false");
				odom_3D_ = false;
			}
   			
   			//Variables
  			float x = 0.0;
  			float y = 0.0;
  			float z = 0.0;  // New, 3D
  			float th = 0.0;
  			float v = 0.0;
  			float vx = 0.0;
  			float vy = 0.0;
  			float vz = 0.0;
 		 	float vth = 0.0;
  			int count = 0;
  			cout << '*' <<endl;
  			ROS_INFO("Sending Odom Transform by rover_odom : %d", send_odom_);
  			if (Low_Battery) ROS_WARN("Battery Low");
			// Start Scanner angle calcultion - Defining variables

			// End Scanner angle calcultion - Defining variables
			int retCode;
  			ros::Time current_time, last_time;
  			current_time = ros::Time::now();
  			last_time = ros::Time::now();
			ros::Rate loop_rate(rate);

			//Initializing Quat_attitude
			Quat_attitude.w = 1;
            		Quat_attitude.x = 0;
            		Quat_attitude.y = 0;
            		Quat_attitude.z = 0;
            		
  			while(n_.ok()){
				// Watch DoG
				if( Speed_control_sq - Speed_control.header.seq > (unsigned int)rate)
				{
					Speed_control.CMD = false;
					Speed_control.RLC  = false;
					if(!Joystick)
					{
						if(debug_) ROS_WARN("No new speed msg has beed received in the last second, Watch dog stops the Rover");
						retCode = rover.setSpeed(0.0,0.0);
					}
				}
				// Start Scanner Angle Loop 

				if (first_cycle && scannerRaw > 0 && count/rate > 2) {
					scanner_hor_corrector = -scannerRaw - scanner_offsetVal;
					ROS_WARN("scanner hor corrector is %f", scanner_hor_corrector);
          ros::param::set("rover_state/scanner_config/hor_val", (double) scanner_hor_corrector);
					first_cycle = false;
				}
				
				// END Scanner Angle Loop
                		EScannerState state1;
                		retCode = rover.getScannerState(state1);


    				Time timestamp;
    				ros::spinOnce();               // check for incoming messages
    				current_time = ros::Time::now();

    				retCode = rover.getSpeedVO(timestamp, v, vth);
    				if (retCode != 0)
				{
     					// Failed to read the rover speed
					ROS_ERROR("Failed to read the rover speed");
   				 }
   				 
   				//Replacing angular velocity with Gyrospeed if vth is not zero
   				if(abs(vth)>0.00001){
   				    //ROS_INFO_ONCE(" Che palle ! vth = %f", vth);
   				    vth = Gyro_velocity.z;
   				} else vth = 0.0;
   				
   				// Calculating 3D Speed and Pose
   				tf::Matrix3x3 Rot_Matrix;
   				tf::Quaternion Quat_attitude_tf;
   				tf::quaternionMsgToTF(Quat_attitude, Quat_attitude_tf);
				Rot_Matrix = tf::Matrix3x3(Quat_attitude_tf);
				tf::Vector3 V_IF_vector =  Rot_Matrix.getColumn(1);
    				// Correction applied
    			if(x_front_config)
				{
					ROS_WARN_ONCE("X_front configuration");
					vy = v * sin(th);    
    				vx = v * cos(th);	
				}	
				else
				{
					ROS_WARN_ONCE("Y_front configuration");
					vy = v * cos(th);
					vx = -v * sin(th);		
				}
				

    				//compute odometry in a typical way given the velocities of the robot
    				float dt = (current_time - last_time).toSec();
    				float delta_y;
    				float delta_x;
    				float delta_z = 0;
    				float delta_th = vth * dt;
    				delta_x = vx * dt;
    				delta_y = vy * dt;
    				if (odom_3D_)
    				{
    					ROS_INFO_ONCE("3D Odometry");
    					delta_x = v*V_IF_vector.getX() * dt;
    					delta_y = v*V_IF_vector.getY() * dt;
    					delta_z = v*V_IF_vector.getZ() * dt;
    				}
    				x += delta_x;
    				y += delta_y;
    				z += delta_z;
    				th += delta_th;

    				//since all odometry is 6DOF we'll need a quaternion created from yaw
    				
  				//tf::Quaternion quat;
  				//quat.setEuler(euler_attitude.x,euler_attitude.y,euler_attitude.z);
  				
  				geometry_msgs::Quaternion odom_quat;
  				if (odom_3D_)
  					odom_quat = Quat_attitude;
  				else
  					odom_quat = tf::createQuaternionMsgFromYaw(th);
  				//tf::quaternionTFToMsg(quat,odom_quat);
    				//geometry_msgs::Quaternion odom_quat = tf::createQuaternionMsgFromYaw(th);
    				// euler_attitude should go to the Quaternion instead

    				//first, we'll publish the transform over tf
    				geometry_msgs::TransformStamped odom_trans;
    				odom_trans.header.stamp = current_time;
    				odom_trans.header.frame_id = "odom";
					if(x_front_config)
						odom_trans.child_frame_id = "base_footprint";
					else
    					odom_trans.child_frame_id = "Imu_link";

    				odom_trans.transform.translation.x = x;
   				odom_trans.transform.translation.y = y;
    				odom_trans.transform.translation.z = z;
    				odom_trans.transform.rotation = odom_quat;

    				//send the transform
    				if (send_odom_ == true) odom_broadcaster.sendTransform(odom_trans);
    				//odom_broadcaster.sendTransform(odom_trans);

    				//next, we'll publish the odometry message over ROS
    				
    				odom.header.stamp = current_time;
   	 			odom.header.frame_id = "odom";
    				

    				//set the position
    				odom.pose.pose.position.x = x;
    				odom.pose.pose.position.y = y;
    				odom.pose.pose.position.z = z;
    				odom.pose.pose.orientation = odom_quat;
				
				donkey_pose.header.stamp = current_time;
   	 			donkey_pose.header.frame_id = "odom";
				donkey_pose.pose = odom.pose;
				

    				//set the velocity
    				odom.child_frame_id = "Imu_link";
    				odom.twist.twist.linear.x = vx;
    				odom.twist.twist.linear.y = vy;
    				odom.twist.twist.linear.z = vz;
    				odom.twist.twist.angular.x = Gyro_velocity.x;
    				odom.twist.twist.angular.y = Gyro_velocity.y;
    				odom.twist.twist.angular.z = Gyro_velocity.z;
    				tw.linear.x = VX;
    				tw.linear.y = VY;

    				//publish the message
    			odom_pub.publish(odom);
				pose_pub.publish(donkey_pose);
    			twist_pub.publish(tw);
				RoverDataProvider();
    			last_time = current_time;
    			//Publishing scanner debug
                Scan_debug.x = scannerRaw;
				Scan_debug.y = scannerCal;
				Scan_debug.z = (scannerCal/244.6);
				Scanner_debug_pub.publish(Scan_debug);
    			count ++;
				Speed_control_sq ++;
				loop_rate.sleep();
				
  				}
		}
				
		

	protected:
		/*state here*/
		ros::NodeHandle n_;
		
		// Subscribers
		ros::Subscriber subFromAttitude_;
		ros::Subscriber subFromJoystick_;
		ros::Subscriber subFromCMDVEL_;
		ros::Subscriber subFromScannerCommander_;
		ros::Subscriber subFromIMU_;
	    ros::Subscriber subFromRightLeftCommands_;
		ros::Subscriber subFromscannerdata_;
		ros::Subscriber subFromscannercommands_;
		ros::Subscriber subFromSpeedcontrol_;
    ros::Subscriber subFromHazardmsg_;
		// Publishers
		ros::Publisher odom_pub;
		ros::Publisher pose_pub;
		ros::Publisher twist_pub;
		ros::Publisher Rover_Track_Speed_pub;
		ros::Publisher Rover_Track_Angles_pub;
		ros::Publisher Rover_Scanner_Data_pub;
		ros::Publisher Rover_Power_Data_pub;
		ros::Publisher Scanner_debug_pub;
		ros::Publisher Scanner_angle_pub;
		
		geometry_msgs::PoseWithCovarianceStamped donkey_pose;
		nav_msgs::Odometry odom;
		tf::TransformBroadcaster odom_broadcaster;
		geometry_msgs::Twist tw;
		geometry_msgs::Vector3 Scan_debug;
		donkey_rover::Rover_Track_Speed outputTrackSpeed;
		donkey_rover::Rover_Scanner outputScanner;
		donkey_rover::Rover_Track_Bogie_Angle outputBogieAngle;
		donkey_rover::Rover_Power_Data outputPower;
		donkey_rover::Speed_control Speed_control;
		uint32_t Speed_control_sq;
		

		int rate = 150;
		bool send_odom_;
		bool odom_3D_;
		bool x_front_config;
        float roll_angle;
        bool new_roll_angle_2 = true;
        bool scanner_horizontal = false;
        bool Joystick = false;
        float scannerRaw_init;
        float scanner_hor_corrector;
        bool first_cycle = true;
        bool madgwick_;
  		geometry_msgs::Vector3 euler_attitude;
  		geometry_msgs::Vector3 Gyro_velocity;
  		geometry_msgs::Quaternion Quat_attitude;
  

	private:

		float temp_adjustment_angle = -100;
        float temp_roll_angle = -100;
        float temp_home_angle = -100;
        float temp_scanner_period = -100;
       	string temp_scanner_command = "Ciao";
        bool Track_eneabled = true;
        // Scanner angle variables
       	float delta_scanner = 0;
		float last_scanner_value = 0;
		float scanner_offsetVal = 0;
		            		
		tf::TransformBroadcaster broadcaster1;
        tf::TransformBroadcaster broadcaster2;
        
		
};



int main(int argc, char **argv)
{
	ros::init(argc, argv, "donkey_rover");
	ros::NodeHandle node;

	DonkeyRoverClass DonkeyRoverNode(node);

	DonkeyRoverNode.Rover_Handle();
	
	DonkeyRoverNode.Scanner_Handle();
	//DonkeyRoverNode.RoverDataProvider();
	DonkeyRoverNode.Odometry_Handle();
	return 0;
}
