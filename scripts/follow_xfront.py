#!/usr/bin/env python
import rospy
import math
import tf
import select
import pdb
import logging

from geometry_msgs.msg import Vector3
import numpy
from tf.transformations import euler_from_quaternion
from sherpa_msgs.msg import Cmd
from sensor_msgs.msg import Joy

### Definig global variables kE and kN
kX = 0
kY = 0
Move = True
exG = 0   #Global Error in x axes
eyG = 0   #Global Error in y axes
error_present = False
joygain = 1
external_gain = 1.0

def cnst(eX,eY,Vmax,b,R):
    global kX,kY
    if eY >= 0 and eY < 0.0001:
	eY = 0.0001
    elif  eY < 0 and eY > -0.0001:
        eY = -0.0001
    ## Calculating the constraints
    if eX >= 0 and eY >= 0:
       kY = Vmax / (eX/eY+2*abs(b)/R)
    if eX >= 0 and eY <= 0:
       kY = Vmax/(eX/eY-2*abs(b)/R)
    if eX <= 0 and eY <= 0:
       kY = -Vmax/(eX/eY+2*abs(b)/R)
    if eX <= 0 and eY >= 0:
       kY = -Vmax/(eX/eY-2*abs(b)/R)
    kX = eX*kY/eY
    ## Absulute values
    kX = math.fabs(kX)
    kY = math.fabs(kY)
    return 0;


def cmd_callback(data):
   global Move
   Move = True
   #if math.fabs(data.param1 - 1) < 0.01:
   #    Move = True
   #else:
   #    Move = False


def joycallback(data):
   global joygain
   if joygain < 5:
   	joygain = joygain + data.buttons[3]
   if joygain > 1:
        joygain = joygain - data.buttons[1]
   if data.buttons[1]==1 or data.buttons[3]==1:
        print "Joygain:   ", joygain
       

def body_error_callback(data):
   global exG,eyG,error_present,external_gain
   exG = data.x
   eyG = data.y
   if data.z > 0.2:
   	external_gain = data.z
   else:
   	external_gain = 1.0
   error_present = True
'''
Sensor Convention  Paper Convention - Controller 
    y                     x
    |                     |
    |                     |
    |_____x               |_____y

'''

def get_param(name, default):
	try:
		v = rospy.get_param(name)
		rospy.loginfo("Found parameter: %s, value: %s"%(name, str(v)))
	except KeyError:
		v = default
		rospy.logwarn("Cannot find value for parameter: %s, assigning "
				"default: %s"%(name, str(v)))
	return v
	
	
def flp():
   # Variables
   global kX,kY,Move,exG,eyG,error_present,joygain,external_gain
   Vmax = 0.8
   R = 0.8
   speed_present = False
   speed_exp = 0.0
   speed_gain = 1
   #eX_Offset = 10
   
   
   # ROS params
   rospy.init_node('donkey_follow')
   rate = rospy.Rate(20)
   
   # Handeling Params
   b = get_param('~distance_b', 0.4)
   Tracking_precision = get_param('~Tracking_precision', 0.5)
   CG = get_param('~Controller_Gain', 1)
   speed_exp = get_param('~Speed', 0)
   if speed_exp == 0:
   	rospy.logwarn("No speed received")
   	speed_present = True
   else:
   	rospy.loginfo("Speed received")
   # Subscribers
   rospy.Subscriber("Cmd", Cmd, cmd_callback)
   rospy.Subscriber("body_error", Vector3, body_error_callback)
   rospy.Subscriber("joy", Joy, joycallback)
   
   #Publishers
   speed_pub = rospy.Publisher('speedfollow', Vector3, queue_size=5)
   
   
   # Pause before executing the command
   #rospy.sleep(10.)
   b_new = b;
   CG_new = CG;
   Tracking_precision_new = Tracking_precision;
   while not rospy.is_shutdown():
      try:
        b_new = rospy.get_param("~distance_b")
        if b_new != b:
            b = b_new
            rospy.logwarn("New value for distance_b = %s received"%str(b))
      except:
        pass
      try:
        CG_new = rospy.get_param("~Controller_Gain")
        if CG_new != CG:
            CG = CG_new
            rospy.logwarn("New value for Controller Gain = %s received"%str(CG))
      except:
        pass
      try:
        Tracking_precision_new = rospy.get_param("~Tracking_precision")
        if Tracking_precision_new != Tracking_precision:
             Tracking_precision = abs(Tracking_precision_new)
             rospy.logwarn("New value for Tracking_precision = %s received"%str(Tracking_precision))
      except:
        pass
      # Move only if it is requested
      if error_present: 
            eX = exG #- b
            eY = eyG
      else:
            #print("ciao")
            eX = 0
            eY = 0
      if (math.fabs(eX) < Tracking_precision and error_present == True and eX != 0):
          rospy.loginfo("controller: x achieved ex: %s"%str(eX))
          eX=0
      if (math.fabs(eY) < Tracking_precision and error_present == True and eY != 0):
          rospy.loginfo("controller: y achieved ey: %s"%str(eY))
          eY=0
      # Calculating the saturation limits
      cnst(eX,eY,Vmax,b,R);
      # Control Action
      #print("Error")
      #print(eX)
      #print(eY)
      Ux = math.atan(external_gain*joygain*CG*eX*speed_gain)*kX*2/3.14159265359
      Uy = math.atan(external_gain*joygain*CG*eY*speed_gain)*kY*2/3.14159265359
      U = numpy.matrix(( (Ux),(Uy) )).transpose()
      # Sending the Control Action 
      speed = Vector3()
      #print("Control xy")
      #print(U)
      VRL = numpy.matrix (( (1,-R/2/b),(1,R/2/b) ))*U
      speed.x = VRL.item(1)  # right hand
      speed.y = VRL.item(0)
      speed.z = 0 
      speed_pub.publish(speed)
      if speed_present: 
      	speed_gain = math.fabs(2*speed_exp/(speed.x+speed.y))
      	if speed_gain > 1.5:
      		speed_gain = 1.5
      rate.sleep()

if __name__ == '__main__':
    try:
        flp()
    except rospy.ROSInterruptException:
        pass
