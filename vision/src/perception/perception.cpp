#include "perception.h"

std::string mesh_array[] = {"cup_eco_orange.pcd",
                            "edeka_red_bowl.pcd",
                            "hela_curry_ketchup.pcd",
                            "ja_milch.pcd",
                            "kellogs_toppas_mini.pcd",
                            "koelln_muesli_knusper_honig_nuss.pcd",
                            "pringles_paprika.pcd",
                            "pringles_salt.pcd",
                            "sigg_bottle.pcd",
                            "tomato_sauce_oro_di_parma.pcd"};

PointCloudRGBPtr cloud_global(new PointCloudRGB),
        cloud_perceived(new PointCloudRGB),
        cloud_aligned(new PointCloudRGB),
        cloud_mesh(new PointCloudRGB);
geometry_msgs::PoseStamped pose_global;

std::string error_message; // Used by the objects_information service
tf::Matrix3x3 global_tf_rotation;

/**
 * Applies all the filters to a PointCloud.
 * @param kinect PointCloud
 * @return Preprocessed PointCloud
 */
PointCloudRGBPtr preprocessCloud(PointCloudRGBPtr kinect) {
    PointCloudRGBPtr cloud_3df(new PointCloudRGB),
            cloud_voxelgridf(new PointCloudRGB),
            cloud_mlsf(new PointCloudRGB),
            cloud_prism(new PointCloudRGB),
            cloud_preprocessed(new PointCloudRGB);
    cloud_3df = apply3DFilter(kinect, 0.4, 0.4, 1.5);   // passthrough filter
    cloud_voxelgridf = voxelGridFilter(cloud_3df);      // voxel grid filter
    cloud_mlsf = mlsFilter(cloud_voxelgridf);           // moving least square filter
    cloud_preprocessed = cloud_mlsf;
    return cloud_preprocessed;
}

/**
 * Segment planes that aren't relevant to the objects.
 * @param cloud_cluster
 */
PointCloudRGBPtr segmentPlanes(PointCloudRGBPtr cloud_cluster) {
    // While a segmented plane would be larger than plane_size_threshold points, segment it.
    bool loop_segmentations = true;
    int segmentations_amount = 0;
    int plane_size_threshold = 8000;
    PointIndices plane_indices(new pcl::PointIndices);
    int c = 0;
    for (int n = 0; loop_segmentations; n++) {
        plane_indices = estimatePlaneIndices(cloud_cluster);

        if (plane_indices->indices.size() > plane_size_threshold)         // is the extracted plane big enough?
        {

            ROS_INFO("plane_indices: %lu", plane_indices->indices.size());
            ROS_INFO("cloud_cluster: %lu", cloud_cluster->points.size());
            cloud_cluster = extractCluster(cloud_cluster, plane_indices, true); // actually extract the object

            n++;
        } else loop_segmentations = false;          // if not big enough, stop looping.
        segmentations_amount = n;
    }
    ROS_INFO("Extracted %d planes!", segmentations_amount);
    return cloud_cluster;
}

/**
 * Find the objects.
 * @param kinect
 * @return
 */
std::vector<PointCloudRGBPtr> findCluster(PointCloudRGBPtr kinect) {

    ros::NodeHandle n;
    std::vector<PointCloudRGBPtr> result;
    CloudTransformer transform_cloud(n);
    PointCloudRGBPtr cloud_cluster(new PointCloudRGB),
            cloud_preprocessed(new PointCloudRGB);
    PointIndices
            plane_indices2(new pcl::PointIndices),
            prism_indices(new pcl::PointIndices);

    ROS_INFO("Starting Cluster extraction");
    savePointCloudRGBNamed(kinect, "1_kinect");

    cloud_preprocessed = preprocessCloud(kinect);
    savePointCloudRGBNamed(cloud_preprocessed, "2_cloud_preprocessed");

    // Delete everything that's not in a cluster with the table
    std::vector<PointCloudRGBPtr> extracted_cloud_preprocessed;
    extracted_cloud_preprocessed = euclideanClusterExtraction(cloud_preprocessed);
    cloud_preprocessed = extracted_cloud_preprocessed[0];

    cloud_preprocessed = transform_cloud.extractAbovePlane(cloud_preprocessed);
    savePointCloudRGBNamed(cloud_preprocessed, "3_extracted_above_plane");

    cloud_cluster = cloud_preprocessed;

    cloud_cluster = segmentPlanes(cloud_cluster);
    savePointCloudRGBNamed(cloud_cluster, "4_cloud_final");

    ROS_INFO("Points after segmentation: %lu", cloud_cluster->points.size());
    cloud_global = cloud_cluster;

    // Split cloud_final into one PointCloud per object

    result = euclideanClusterExtraction(cloud_cluster);

    ROS_INFO("CALCULATED RESULT!");


    if (cloud_global->points.size() == 0) {
        ROS_ERROR("Extracted Cluster is empty");
        error_message = "Final extracted cluster was empty. ";
    } else {
        ROS_INFO("%sExtraction OK", "\x1B[32m");
        error_message = "";
    }

    for (int i = 0; i < result.size(); i++) {
        std::stringstream obj_files;
        obj_files << "object_" << i;
        savePointCloudRGBNamed(result[i], obj_files.str());
    }

    return result;
}

/**
 * Finds the geometrical center and rotation of an object.
 * @param The pointcloud object_cloud
 * @return The pose of the object contained in object_cloud
 */
geometry_msgs::PoseStamped findPose(const PointCloudRGBPtr input, std::string label) {
    // instantiate objects for results

    geometry_msgs::PoseStamped current_pose, map_pose;
    tf::Quaternion quat_tf, quat_rot;
    geometry_msgs::QuaternionStamped quat_msg;
    Eigen::Vector4f centroid;
    tf::TransformListener t_listener;

    std::string map = "map";
    std::string kinect_frame = "head_mount_kinect_rgb_optical_frame";

    ROS_INFO("Starting pose estimation");
    // add header and time
    current_pose.header.stamp = ros::Time(0);
    current_pose.header.frame_id = kinect_frame;

    // Calculate quaternions
    cloud_mesh = getTargetByLabel(label, centroid);

    ROS_INFO("Alignment...");
    // initial alignment
    cloud_aligned = iterativeClosestPoint(cloud_mesh, input);

    ROS_INFO("Calculating centroid");
    // calculate and set centroid from mesh
    pcl::compute3DCentroid(*cloud_aligned, centroid);
    current_pose.pose.position.x = centroid.x();
    current_pose.pose.position.y = centroid.y();
    current_pose.pose.position.z = centroid.z();

    ROS_INFO("Calculating quaternion");

    // calculate quaternion
    tf::StampedTransform t_transform, map_transform, rotated_transform;

    t_transform.setBasis(global_tf_rotation);
    quat_tf = t_transform.getRotation();
    quat_tf.normalize();
    quat_msg.quaternion.x = quat_tf.x();
    quat_msg.quaternion.y = quat_tf.y();
    quat_msg.quaternion.z = quat_tf.z();
    quat_msg.quaternion.w = quat_tf.w();


    if (global_tf_rotation.getRow(2).z() > 0.0) {
        ROS_INFO("Wrong rotation! Flipping quaternion");

        quat_rot.setX(0.0);
        quat_rot.setY(0.0);
        quat_rot.setZ(-1.0);
        quat_rot.setW(0.0);
        quat_rot.normalize();
        quat_tf *= quat_rot;
        quat_tf.normalize();
        quat_msg.quaternion.x = quat_tf.x();
        quat_msg.quaternion.y = quat_tf.y();
        quat_msg.quaternion.z = quat_tf.z();
        quat_msg.quaternion.w = quat_tf.w();
    }

    ROS_INFO("Quaternion ready ");
    current_pose.pose.orientation = quat_msg.quaternion;

    pose_global = current_pose;

    ROS_INFO("POSE ESTIMATION DONE");
    return current_pose;
}

/**
 * Estimates surface normals.
 * @param Pointcloud input
 * @return The estimated surface normals of the input Pointcloud
 */
PointCloudNormalPtr estimateSurfaceNormals(PointCloudRGBPtr input) {
    ROS_INFO("ESTIMATING SURFACE NORMALS");


    pcl::NormalEstimation<pcl::PointXYZRGB, pcl::Normal> ne;
    ne.setInputCloud(input);
    pcl::search::KdTree<pcl::PointXYZRGB>::Ptr tree(
            new pcl::search::KdTree<pcl::PointXYZRGB>());
    ne.setSearchMethod(tree);

    PointCloudNormalPtr cloud_normals(new PointCloudNormal);

    ne.setRadiusSearch(0.03); // Use all neighbors in a sphere of radius 3cm

    ne.compute(*cloud_normals);

    ROS_INFO("SURFACE NORMALS ESTIMATED SUCCESSFULLY!");
    return cloud_normals;
}

/**
 * Applies a PassThrough filter to all dimensions, reducing points and
 * narrowing field of vision.
 * @param input Pointcloud
 * @param x
 * @param y
 * @param z
 * @return Filtered Pointcloud
 */
PointCloudRGBPtr apply3DFilter(PointCloudRGBPtr input,
                               float x,
                               float y,
                               float z) {


    ROS_INFO("Starting passthrough filter");
    PointCloudRGBPtr input_after_x(new PointCloudRGB),
            input_after_xy(new PointCloudRGB), input_after_xyz(new PointCloudRGB);
    /** Create the filtering object **/
    // Create the filtering object (x-axis)
    pcl::PassThrough<pcl::PointXYZRGB> pass;
    pass.setInputCloud(input);
    pass.setFilterFieldName("x");
    pass.setFilterLimits(-x, x);

    //pass.setUserFilterValue(0.0f);
    pass.setKeepOrganized(false);
    pass.filter(*input_after_x);

    // Create the filtering object (y-axis)
    pass.setInputCloud(input_after_x);
    pass.setFilterFieldName("y");
    pass.setFilterLimits(-y, y);

    //pass.setUserFilterValue(0.0f);
    pass.setKeepOrganized(false);
    pass.filter(*input_after_xy);

    // Create the filtering object (z-axis)
    pass.setInputCloud(input_after_xy);
    pass.setFilterFieldName("z");
    pass.setFilterLimits(
            0.0, z); // no negative range (the pr2 can't look behind its head)

    //pass.setUserFilterValue(0.0f);
    pass.setKeepOrganized(true);
    pass.filter(*input_after_xyz);

    if (input_after_xyz->points.size() == 0) {
        ROS_ERROR("Cloud empty after passthrough filtering");
        error_message = "Cloud was empty after filtering. ";
    }


    return input_after_xyz;
}

/**
 * Estimates plane indices of a PointCloud.
 * @param input PointCloud
 * @return Indices of the plane points in the PointCloud.
 */
PointIndices estimatePlaneIndices(PointCloudRGBPtr input) {

    ROS_INFO("Starting plane indices estimation");
    PointIndices planeIndices(new pcl::PointIndices);
    pcl::ModelCoefficients::Ptr coefficients(new pcl::ModelCoefficients);
    pcl::SACSegmentation<pcl::PointXYZRGB> segmentation;

    segmentation.setInputCloud(input);
    segmentation.setModelType(pcl::SACMODEL_PLANE);
    segmentation.setMethodType(pcl::SAC_RANSAC);
    segmentation.setDistanceThreshold(0.01); // Distance to model points
    segmentation.setOptimizeCoefficients(true);
    segmentation.segment(*planeIndices, *coefficients);


    if (planeIndices->indices.size() == 0) {
        ROS_ERROR("No plane (indices) found");
        error_message = "No plane found. ";
    }

    return planeIndices;
}

/**
 * Extracts a PointCloud from an input PointCloud, using indices.
 * @param input PointCloud
 * @param indices
 * @param bool negative to decide whether to return all points fulfilling the indices,
 * or all points not fulfilling the indices.
 * @return Extracted PointCloud
 */
PointCloudRGBPtr extractCluster(PointCloudRGBPtr input,
                                PointIndices indices,
                                bool negative) {
    ROS_INFO("CLUSTER EXTRACTION");
    PointCloudRGBPtr result(new PointCloudRGB);

    PointCloudRGBPtr objects(new PointCloudRGB);
    pcl::ExtractIndices<pcl::PointXYZRGB> extract;
    extract.setInputCloud(input);
    extract.setIndices(indices);
    extract.setNegative(negative);
    extract.setKeepOrganized(false);
    extract.filter(*objects);
    ROS_INFO("CLUSTER EXTRACTION COMPLETED!");

    return objects;
}

/**
 * Filters the input cloud with a moving least squares algorithm.
 * @param PointCloud input
 * @return The filtered PointCloud
 */
PointCloudRGBPtr mlsFilter(PointCloudRGBPtr input) {
    ROS_INFO("MLS Filter!");
    PointCloudRGBPtr result(new PointCloudRGB);

    int poly_ord = 1;

    pcl::search::KdTree<pcl::PointXYZRGB>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZRGB>);
    pcl::PointCloud<pcl::PointNormal> mls_points;
    pcl::MovingLeastSquares<pcl::PointXYZRGB, pcl::PointNormal> mls;

    mls.setComputeNormals(true);
    mls.setInputCloud(input);
    mls.setPolynomialOrder(poly_ord); // the lower the smoother, the higher the more exact
    mls.setSearchMethod(tree);
    mls.setSearchRadius(0.03);
    mls.process(mls_points);

    for (int i = 0; i < mls_points.size(); i++) {
        pcl::PointXYZRGB point;

        point.x = mls_points.points[i].x;
        point.y = mls_points.points[i].y;
        point.z = mls_points.points[i].z;
        point.r = input->points[i].r;
        point.g = input->points[i].g;
        point.b = input->points[i].b;
        point.rgb = input->points[i].rgb;
        point.rgba = input->points[i].rgba;
        result->push_back(point);
    }
    ROS_INFO("size: %d", result->size());
    ROS_INFO("Finished MLS Filter!");
    return result;
}


/**
 * Filters the input cloud with a voxel grid filter.
 * @param PointCloud input
 * @return Filtered PointCloud
 */
PointCloudRGBPtr voxelGridFilter(PointCloudRGBPtr input) {
    PointCloudRGBPtr result(new PointCloudRGB);

    pcl::VoxelGrid<pcl::PointXYZRGB> sor;
    sor.setInputCloud(input);
    sor.setLeafSize(0.005f, 0.005f, 0.005f);
    sor.filter(*result);
    ROS_INFO("size: %d", result->size());
    return result;
}

/**
 * Estimates features of an object in a PointCloud using VFHSignature308.
 * @param input PointCloud
 * @return VFHSignature308 Features
 */
PointCloudVFHS308Ptr cvfhRecognition(PointCloudRGBPtr input) {
    ROS_INFO("CVFH Recognition!");
    // Object for storing the normals.
    pcl::PointCloud<pcl::Normal>::Ptr normals(new pcl::PointCloud<pcl::Normal>);
    // Object for storing the CVFH descriptors.
    PointCloudVFHS308Ptr descriptors(new pcl::PointCloud<pcl::VFHSignature308>);

    // Estimate the normals of the object.
    normals = estimateSurfaceNormals(input);

    // New KdTree to search with.
    pcl::search::KdTree<pcl::PointXYZRGB>::Ptr kdtree(new pcl::search::KdTree<pcl::PointXYZRGB>);

    // CVFH estimation object.
    pcl::CVFHEstimation<pcl::PointXYZRGB, pcl::Normal, pcl::VFHSignature308> cvfh;
    cvfh.setInputCloud(input);
    cvfh.setInputNormals(normals);
    cvfh.setSearchMethod(kdtree);
    cvfh.setEPSAngleThreshold(5.0 / 180.0 * M_PI); // 5 degrees.
    cvfh.setCurvatureThreshold(1.0);
    cvfh.setNormalizeBins(false);
    ROS_INFO("CVFH recognition parameters set. Computing now...");
    cvfh.compute(*descriptors);
    ROS_INFO("CVFH features computed successfully!");

    return descriptors; // to vector
}

/**
 * Seperates clusters from each other using euclidean cluster extraction.
 * @param input PointCloud
 * @return Seperated PointClouds
 */
std::vector<PointCloudRGBPtr> euclideanClusterExtraction(PointCloudRGBPtr input) {
    ROS_INFO("Euclidean Cluster Extraction!");
    pcl::search::KdTree<pcl::PointXYZRGB>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZRGB>);
    tree->setInputCloud(input);

    PointIndicesVector cluster_indices;
    pcl::EuclideanClusterExtraction<pcl::PointXYZRGB> ec;
    ec.setClusterTolerance(0.01);
    ec.setMinClusterSize(100);
    ec.setMaxClusterSize(100000);
    ec.setSearchMethod(tree);
    ec.setInputCloud(input);
    ROS_INFO("BEFORE EXTRACT");
    ec.extract(cluster_indices);


    ROS_INFO("AFTER EXTRACT");

    std::vector<PointCloudRGBPtr> result;

    int j = 0;
    for (std::vector<pcl::PointIndices>::const_iterator it = cluster_indices.begin();
         it != cluster_indices.end(); ++it) {
        PointCloudRGBPtr cloud_cluster(new PointCloudRGB);
        for (std::vector<int>::const_iterator pit = it->indices.begin(); pit != it->indices.end(); ++pit)
            cloud_cluster->points.push_back(input->points[*pit]);
        cloud_cluster->width = cloud_cluster->points.size();
        cloud_cluster->height = 1;
        cloud_cluster->is_dense = true;

        std::cout << "PointCloud representing the Cluster: " << cloud_cluster->points.size() << " data points."
                  << std::endl;
        j++;

        result.push_back(cloud_cluster);
    }


    ROS_INFO("Finished Euclidean Cluster Extraction!");
    return result;
}


/**
 * Calculates the alignment of an object to a certain target using iterative closest point algorithm.
 * @param input PointCloud
 * @param target PointCloud
 * @return output PointCloud
 */
PointCloudRGBPtr iterativeClosestPoint(PointCloudRGBPtr input,
                                       PointCloudRGBPtr target) {

    PointCloudRGBPtr result(new PointCloudRGB), input_centroid(new PointCloudRGB);


    pcl::IterativeClosestPoint<pcl::PointXYZRGB, pcl::PointXYZRGB> icp;
    icp.setInputSource(input);
    icp.setInputTarget(target);
    icp.setMaximumIterations(20000);
    icp.setMaxCorrespondenceDistance(6.0f); // set Max distance btw source <-> target to include into estimation

    PointCloudRGBPtr final(new PointCloudRGB);
    icp.align(*final);
    std::cout << "has converged:" << icp.hasConverged() << " score: " <<
              icp.getFitnessScore() << std::endl;
    std::cout << icp.getFinalTransformation() << std::endl;
    Eigen::Matrix4f transformation = icp.getFinalTransformation();
    tf::Matrix3x3 tf_rotation(transformation(0, 0),
                              transformation(0, 1),
                              transformation(0, 2),
                              transformation(1, 0),
                              transformation(1, 1),
                              transformation(1, 2),
                              transformation(2, 0),
                              transformation(2, 1),
                              transformation(2, 2));


    global_tf_rotation = tf_rotation;


    return final;
}

/**
 * Gets the color histogram from a PointCloud.
 * @param Input PointCloud cloud
 * @return Concatenated floats (r,g,b) from PointCloud points
 */
std::vector<uint64_t> produceColorHist(PointCloudRGBPtr cloud) {
    int red[8];
    int green[8];
    int blue[8];
    std::vector<uint64_t> result;

    // initialize all array-values with 0
    for (int i = 0; i < 8; i++) {
        red[i] = 0;
        green[i] = 0;
        blue[i] = 0;
    }

    for (int i = 0; i < cloud->size(); i++) {
        pcl::PointXYZRGB p = cloud->points[i];
        // increase value in bin at given index
        if (p.r < 32) {
            red[0]++;

        } else if (p.r >= 32 && p.r < 64) {
            red[1]++;

        } else if (p.r >= 64 && p.r < 96) {
            red[2]++;
        } else if (p.r >= 96 && p.r < 128) {
            red[3]++;

        } else if (p.r >= 128 && p.r < 160) {
            red[4]++;

        } else if (p.r >= 160 && p.r < 192) {
            red[5]++;

        } else if (p.r >= 192 && p.r < 224) {
            red[6]++;

        } else if (p.r >= 224 && p.r < 256) {
            red[7]++;

        }

        if (p.g < 32) {
            green[0]++;

        } else if (p.g >= 32 && p.g < 64) {
            green[1]++;

        } else if (p.g >= 64 && p.g < 96) {
            green[2]++;
        } else if (p.g >= 96 && p.g < 128) {
            green[3]++;

        } else if (p.g >= 128 && p.g < 160) {
            green[4]++;

        } else if (p.g >= 160 && p.g < 192) {
            green[5]++;

        } else if (p.g >= 192 && p.g < 224) {
            green[6]++;

        } else if (p.g >= 224 && p.g < 256) {
            green[7]++;

        }

        if (p.b < 32) {
            blue[0]++;

        } else if (p.b >= 32 && p.b < 64) {
            blue[1]++;

        } else if (p.b >= 64 && p.b < 96) {
            blue[2]++;
        } else if (p.b >= 96 && p.b < 128) {
            blue[3]++;

        } else if (p.b >= 128 && p.b < 160) {
            blue[4]++;

        } else if (p.b >= 160 && p.b < 192) {
            blue[5]++;

        } else if (p.b >= 192 && p.b < 224) {
            blue[6]++;

        } else if (p.b >= 224 && p.b < 256) {
            blue[7]++;

        }
    }

    // concatenate red, green and blue entries
    for (int r = 0; r < 8; r++) {
        result.push_back(red[r]);
    }
    for (int g = 0; g < 8; g++) {
        result.push_back(green[g]);
    }
    for (int b = 0; b < 8; b++) {
        result.push_back(blue[b]);
    }

    return result;

}


/**
 * Gets the CVFH features from PointClouds.
 * @param all_clusters PointCloud
 * @return CVFH features, a histogram of angles between a central viewpoint direction and each normal
 */
std::vector<float> getCVFHFeatures(std::vector<PointCloudRGBPtr> all_clusters) {

    PointCloudVFHS308Ptr vfhs(new pcl::PointCloud<pcl::VFHSignature308>);
    std::vector<float> result;


    for (int i = 0; i < all_clusters.size(); i++) {

        vfhs = cvfhRecognition(all_clusters[i]);

        for (int x = 0; x < 308; x++) {
            //ROS_INFO("%f", current_features[x]);
            result.push_back(vfhs->points[0].histogram[x]);
        }
    }

    return result;
}


/**
 * Gets the color features from a PointCloud.
 * @param all_clusters PointCloud
 * @param color_features_vector to be filled
 */
std::vector<uint64_t> getColorFeatures(std::vector<PointCloudRGBPtr> all_clusters) {

    std::vector<uint64_t> current_color_features;
    std::vector<uint64_t> result;

    for (int i = 0; i < all_clusters.size(); i++) {
        current_color_features = produceColorHist(all_clusters[i]);

        for (int x = 0; x < 24; x++) {
            //ROS_INFO("%f", current_color_features[x]);
            result.push_back(current_color_features[x]);
        }
    }

    return result;
}

/**
 * Load the correct PCD file for the label given.
 * @param label
 * @return Object PointCloud out of PCD file
 */
PointCloudRGBPtr getTargetByLabel(std::string label, Eigen::Vector4f centroid) {
    PointCloudRGBPtr result(new PointCloudRGB),
            mesh(new PointCloudRGB);

    if (label == "PringlesPaprika") {
        pcl::io::loadPCDFile("../../../src/vision_suturo_1718/vision/meshes/pringles.pcd", *mesh);

    } else if (label == "PringlesSalt") {
        pcl::io::loadPCDFile("../../../src/vision_suturo_1718/vision/meshes/pringles.pcd", *mesh);
    } else if (label == "SiggBottle") {
        pcl::io::loadPCDFile("../../../src/vision_suturo_1718/vision/meshes/sigg_bottle.pcd", *mesh);
    } else if (label == "JaMilch") {
        pcl::io::loadPCDFile("../../../src/vision_suturo_1718/vision/meshes/ja_milch.pcd", *mesh);
    } else if (label == "TomatoSauceOroDiParma") {
        pcl::io::loadPCDFile("../../../src/vision_suturo_1718/vision/meshes/tomato_sauce_oro_di_parma.pcd", *mesh);
    } else if (label == "KoellnMuesliKnusperHonigNuss") {
        pcl::io::loadPCDFile("../../../src/vision_suturo_1718/vision/meshes/koelln_muesli_knusper_honig_nuss.pcd",
                             *mesh);
    } else if (label == "KelloggsToppasMini") {
        pcl::io::loadPCDFile("../../../src/vision_suturo_1718/vision/meshes/kelloggs_toppas_mini.pcd", *mesh);
    } else if (label == "HelaCurryKetchup") {
        pcl::io::loadPCDFile("../../../src/vision_suturo_1718/vision/meshes/hela_curry_ketchup.pcd", *mesh);
    } else if (label == "CupEcoOrange") {
        pcl::io::loadPCDFile("../../../src/vision_suturo_1718/vision/meshes/cup_eco_orange.pcd", *mesh);
    } else if (label == "EdekaRedBowl") {
        pcl::io::loadPCDFile("../../../src/vision_suturo_1718/vision/meshes/edeka_red_bowl.pcd", *mesh);
    }

    return mesh;
}