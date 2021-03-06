//
// Created by Alex on 20.03.18.
//

#include <iostream>

#include "../perception/perception.h"
#include <opencv2/ml.hpp>
#include <dirent.h>
#include <ros/package.h>

#include <cv.h>       // opencv general include file
#include <ml.h>		  // opencv machine learning include file
#include <stdio.h>
#include <stdlib.h>

using namespace cv; // OpenCV API is in the C++ "cv" namespace

class classifier {
private:
    Ptr<cv::ml::RTrees> random_trees_color_classifier;
    Ptr<cv::ml::RTrees> random_trees_cvfh_classifier;
    int NUMBER_OF_TRAINING_SAMPLES = 2165; // 2165, einzelnd ~217, JaMilch + Salt = 436
    int COLOR_ATTRIBUTES_PER_SAMPLE = 24;
    int CVFH_ATTRIBUTES_PER_SAMPLE = 308;
    int sample_counter = 0;
    cv::Mat color_training_data = Mat(NUMBER_OF_TRAINING_SAMPLES, COLOR_ATTRIBUTES_PER_SAMPLE, CV_32FC1); // Input data
    cv::Mat cvfh_training_data = Mat(NUMBER_OF_TRAINING_SAMPLES, CVFH_ATTRIBUTES_PER_SAMPLE, CV_32FC1); // Input data
    cv::Mat responses = Mat(NUMBER_OF_TRAINING_SAMPLES, 1, CV_32SC1);

    std::string labels[10] = {  "CupEcoOrange",
                                "EdekaRedBowl",
                                "HelaCurryKetchup",
                                "JaMilch",                      // 3
                                "KellogsToppasMini",
                                "KoellnMuesliKnusperHonigNuss", // 5
                                "PringlesPaprika",              // 6
                                "PringlesSalt",                 // 7
                                "SiggBottle",
                                "TomatoSauceOroDiParma"};       // 5 - 3 - 6|7
    std::string pkg_path = ros::package::getPath("vision_suturo");

public:
    classifier();
    bool train(std::string directory, bool update);
    std::string classify(std::vector<uint64_t> color_features, std::vector<float> cvfh_features);
    bool has_suffix(std::string s, std::string suffix);
    std::vector<float> read_from_file(std::string full_path, std::vector<float> parsedCsv);

};

