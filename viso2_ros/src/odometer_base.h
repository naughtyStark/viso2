
#ifndef ODOMETER_BASE_H_
#define ODOMETER_BASE_H_

#include <ros/ros.h>
#include <nav_msgs/Odometry.h>
#include <std_srvs/Empty.h>

#include <tf/transform_listener.h>
#include <tf/transform_broadcaster.h>

#include <tf2/LinearMath/Quaternion.h>

namespace viso2_ros
{

/**
 * Base class for odometers, handles tf's, odometry and pose
 * publishing. This can be used as base for any incremental pose estimating
 * sensor. Sensors that measure velocities cannot be used.
 */
class OdometerBase
{

private:

  // publisher
  ros::Publisher odom_pub_;
  ros::Publisher pose_pub_;

  ros::ServiceServer reset_service_;

  // tf related
  std::string sensor_frame_id_;
  std::string odom_frame_id_;
  std::string base_link_frame_id_;
  tf::TransformListener tf_listener_;
  tf::TransformBroadcaster tf_broadcaster_;
  bool publish_tf_;
  bool invert_tf_;

  // the current integrated camera pose
  tf::Transform integrated_pose_;
  // timestamp of the last update
  ros::Time last_update_time_;

  // covariances
  boost::array<double, 36> pose_covariance_;
  boost::array<double, 36> twist_covariance_;

public:

  OdometerBase()
  {
    // Read local parameters
    ros::NodeHandle local_nh("~");

    local_nh.param("odom_frame_id", odom_frame_id_, std::string("/odom"));
    local_nh.param("base_link_frame_id", base_link_frame_id_, std::string("/base_link"));
    local_nh.param("sensor_frame_id", sensor_frame_id_, std::string("/camera")); 
    local_nh.param("publish_tf", publish_tf_, true);
    local_nh.param("invert_tf", invert_tf_, false);

    ROS_INFO_STREAM("Basic Odometer Settings:" << std::endl <<
                    "  odom_frame_id      = " << odom_frame_id_ << std::endl <<
                    "  base_link_frame_id = " << base_link_frame_id_ << std::endl <<
                    "  sensor_frame_id    = " << sensor_frame_id_ << std::endl <<
                    "  publish_tf         = " << (publish_tf_?"true":"false") << std::endl <<
                    "  invert_tf          = " << (invert_tf_?"true":"false"));

    // advertise
    odom_pub_ = local_nh.advertise<nav_msgs::Odometry>("odometry", 1);
    pose_pub_ = local_nh.advertise<geometry_msgs::PoseStamped>("pose", 1);

    reset_service_ = local_nh.advertiseService("reset_pose", &OdometerBase::resetPose, this);

    integrated_pose_.setIdentity();

    pose_covariance_.assign(0.0);
    twist_covariance_.assign(0.0);
  }

protected:

  void setSensorFrameId(const std::string& frame_id)
  {
    sensor_frame_id_ = frame_id;
  }

  std::string getSensorFrameId() const
  {
    return sensor_frame_id_;
  }

  void setPoseCovariance(const boost::array<double, 36>& pose_covariance)
  {
    pose_covariance_ = pose_covariance;
  }

  void setTwistCovariance(const boost::array<double, 36>& twist_covariance)
  {
    twist_covariance_ = twist_covariance;
  }

  void integrateAndPublish(const tf::Transform& delta_transform, const ros::Time& timestamp)
  {
    if (sensor_frame_id_.empty())
    {
      ROS_ERROR("[odometer] update called with unknown sensor frame id!");
      return;
    }
    if (timestamp < last_update_time_)
    {
      ROS_WARN("[odometer] saw negative time change in incoming sensor data, resetting pose.");
      integrated_pose_.setIdentity();
      tf_listener_.clear();
    }

    integrated_pose_ *= delta_transform;
     
    tf::StampedTransform base_to_sensor;
    std::string error_msg;
    if (tf_listener_.canTransform(base_link_frame_id_, sensor_frame_id_, timestamp, &error_msg))
    {
      tf_listener_.lookupTransform(
          base_link_frame_id_,
          sensor_frame_id_,
          timestamp, base_to_sensor);
    }
    else
    {
      ROS_WARN_THROTTLE(10.0, "The tf from '%s' to '%s' does not seem to be available, "
                              "will assume it as identity!",
                              base_link_frame_id_.c_str(),
                              sensor_frame_id_.c_str());
      ROS_DEBUG("Transform error: %s", error_msg.c_str());
      base_to_sensor.setIdentity();
    }

    tf::Transform base_transform = base_to_sensor * integrated_pose_ * base_to_sensor.inverse();

    // BMNF: Initial position when the robot is not in NED origin.
    tf::Quaternion q2(0.02478365567, -0.08955947924, 0.5741207366, 0.8134803316) ; // Portals Vells
    tf::Vector3 t(74.568675009499998, 18.5392068604, 5.4271955410699997) ; // Portals Vells

    // tf::Quaternion q2(-0.02665389152, 0.1151081534, 0.5226100899, 0.8443449396) ;
    // tf::Vector3 t(-40.34574719, -46.88363208, 1.162873993) ;


    // tf::Quaternion q2(0.01229814164, -0.05866396396, 0.9742064727, 0.2175523926) ;
    // tf::Quaternion q2(0.06656106726, 0.02654796015, -0.9259859207, 0.3706951643) ;
    // tf::Quaternion q2(-0.03900879093, 0.05679888864, -0.2387434845, 0.9686349927) ;
    // tf::Quaternion q2(-0.02564399815, 0.06341876553, 0.4646516965, 0.882847238461) ;
    
    
    // tf::Vector3 t(58.33139893, -1.011998077, 0.1093880108) ; 
    // tf::Vector3 t(1.05552999574, 26.1954855264, 0.14202065041) ;                  
    // tf::Vector3 t(-19.84049806, 16.37197326, 0.1311650139) ;                                                                                                                                                   

    tf::Transform EKF_initial_pose ;

    EKF_initial_pose.setOrigin(t) ;
    EKF_initial_pose.setRotation(q2) ;

    // BMNF: transform between NED origin and initial position of the robot when odometer starts.
    base_transform = EKF_initial_pose * base_transform ;

    nav_msgs::Odometry odometry_msg;
    odometry_msg.header.stamp = timestamp;
    odometry_msg.header.frame_id = odom_frame_id_;
    odometry_msg.child_frame_id = base_link_frame_id_;
    tf::poseTFToMsg(base_transform, odometry_msg.pose.pose);

    tf::Transform delta_base_transform = base_to_sensor * delta_transform * base_to_sensor.inverse();
    if (!last_update_time_.isZero())
    {
      double delta_t = (timestamp - last_update_time_).toSec();
      if (delta_t)
      {
        odometry_msg.twist.twist.linear.x = delta_base_transform.getOrigin().getX() / delta_t;
        odometry_msg.twist.twist.linear.y = delta_base_transform.getOrigin().getY() / delta_t;
        odometry_msg.twist.twist.linear.z = delta_base_transform.getOrigin().getZ() / delta_t;
        tf::Quaternion delta_rot = delta_base_transform.getRotation();
        tfScalar angle = delta_rot.getAngle();
        tf::Vector3 axis = delta_rot.getAxis();
        tf::Vector3 angular_twist = axis * angle / delta_t;
        odometry_msg.twist.twist.angular.x = angular_twist.x();
        odometry_msg.twist.twist.angular.y = angular_twist.y();
        odometry_msg.twist.twist.angular.z = angular_twist.z();
      }
    }

    odometry_msg.pose.covariance = pose_covariance_;
    odometry_msg.twist.covariance = twist_covariance_;
    odom_pub_.publish(odometry_msg);

    geometry_msgs::PoseStamped pose_msg;
    pose_msg.header.stamp = odometry_msg.header.stamp;
    pose_msg.header.frame_id = odometry_msg.header.frame_id;
    pose_msg.pose = odometry_msg.pose.pose;

    pose_pub_.publish(pose_msg);

    if (publish_tf_)
    {
      if (invert_tf_)
      {
        tf_broadcaster_.sendTransform(
            tf::StampedTransform(base_transform.inverse(), timestamp,
	    base_link_frame_id_, odom_frame_id_));
      }
      else
      {
        tf_broadcaster_.sendTransform(
            tf::StampedTransform(base_transform, timestamp,
            odom_frame_id_, base_link_frame_id_));
      }
    }

    last_update_time_ = timestamp;
  }


  bool resetPose(std_srvs::Empty::Request&, std_srvs::Empty::Response&)
  {
    integrated_pose_.setIdentity();
    return true;
  }

};

} // end of namespace

#endif

