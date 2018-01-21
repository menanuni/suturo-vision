//
// Created by tammo on 21.01.18.
//

#ifndef VISION_CLOUDCONTAINER_H
#define VISION_CLOUDCONTAINER_H

#include <sensor_msgs/PointCloud2.h>

#include "../short_types.h"



class CloudContainer {


    PointCloudXYZPtr kinect;
    std::vector<sensor_msgs::PointCloud2> objects;

public:

    CloudContainer();

    void setInputCloud(PointCloudXYZPtr input);

    void setObjectClouds(std::vector<sensor_msgs::PointCloud2> object_clouds);

    const PointCloudXYZPtr &getKinect() const {
        return kinect;
    }

    const std::vector<sensor_msgs::PointCloud2> &getObjects() const {
        return objects;
    }

    virtual ~CloudContainer() {

    }
};

#endif //VISION_CLOUDCONTAINER_H
