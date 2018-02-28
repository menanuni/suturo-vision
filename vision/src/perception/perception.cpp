#include "perception.h"

geometry_msgs::PointStamped centroid_stamped_perc;
int best_ia_index = 0;

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

enum mesh_enum {
    CUP_ECO_ORANGE,
    EDEKA_RED_BOWL,
    HELA_CURRY_KETCHUP,
    JA_MILCH,
    KELLOGS_TOPPAS_MINI,
    KOELLN_MUESLI_KNUSPER_HONIG_NUSS,
    PRINGLES_PAPRIKA,
    PRINGLES_SALT,
    SIGG_BOTTLE,
    TOMATO_SAUCE_ORO_DI_PARMA
};

std::string error_message_perc;

PointCloudRGBPtr cloud_global(new PointCloudRGB);


PointCloudRGBPtr preprocessCloud(PointCloudRGBPtr kinect) {
    PointCloudRGBPtr cloud_3df(new PointCloudRGB),
            cloud_voxelgridf(new PointCloudRGB),
            cloud_mlsf(new PointCloudRGB),
            cloud_prism(new PointCloudRGB),
            cloud_preprocessed(new PointCloudRGB);
    cloud_3df = apply3DFilter(kinect, 0.4, 0.4, 1.5);   // passthrough filter
// std::cout << "after 3dfilter cluster is of size: " << cloud_3df->size() << std::endl;
    cloud_voxelgridf = voxelGridFilter(cloud_3df);      // voxel grid filter
// std::cout << "after vgfilter cluster is of size: " << cloud_voxelgridf->size() << std::endl;
    cloud_mlsf = mlsFilter(cloud_voxelgridf);           // moving least square filter
//std::cout << "after mlsfilter cluster is of size: " << cloud_mlsf->size() << std::endl;
    cloud_preprocessed = cloud_mlsf; // cloud_f set after last filtering function is applied
    return cloud_preprocessed;
}

void segmentPlanes(PointCloudRGBPtr cloud_cluster){
    // While a segmented plane would be larger than plane_size_threshold points, segment it.
    bool loop_segmentations = true;
    int segmentations_amount = 0;
    int plane_size_threshold = 12000;
    PointIndices plane_indices(new pcl::PointIndices);
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
}
/**
 * Find the object!
 * @param kinect
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

    if (kinect->points.size() < 500)                        // if PR2 is not looking at anything
    {
        ROS_ERROR("Input from kinect is empty");
        error_message_perc = "Cloud empty. ";
        centroid_stamped_perc = findCenterGazebo();         // Use gazebo data instead
    } else {
        ROS_INFO("Starting Cluster extraction");

        cloud_preprocessed = preprocessCloud(kinect);

        cloud_preprocessed = transform_cloud.extractAbovePlane(cloud_preprocessed);
        cloud_cluster = cloud_preprocessed;

        segmentPlanes(cloud_cluster);
        cloud_global = cloud_cluster;

        /*
         * We currently don't need outlierRemoval here, because euclideanClusterExtraction
         * already has a set minimum point value, which causes smaller clusters / amounts of outliers  to be extracted
         * anyway.
        */

        // Split cloud_final into one PointCloud per object
        std::vector<PointCloudRGBPtr> result = euclideanClusterExtraction(cloud_cluster);
        ROS_INFO("CALCULATED RESULT!");


        if (cloud_global->points.size() == 0) {
            ROS_ERROR("Extracted Cluster is empty");
            error_message_perc = "Final extracted cluster was empty. ";
            centroid_stamped_perc = findCenterGazebo(); // Use gazebo data instead
        } else {
            ROS_INFO("%sExtraction OK", "\x1B[32m");
        }

        error_message_perc = "";

        /**
        for (int i = 0; i < result.size(); i++){
            std::stringstream obj_files;
            obj_files << "object_" << i;
            savePointCloudRGBNamed(result[i], obj_files.str());

        }
        **/

        return result;

    }
}

/**
 * Returns a fake point (not estimated) with its origin in the model in simulation.
 * It is only called, when findCluster() cannot find a point in simulation
 * @return
 */
geometry_msgs::PointStamped
findCenterGazebo() {
    centroid_stamped_perc.point.x = 0, centroid_stamped_perc.point.y = 0, centroid_stamped_perc.point.z = 0;
    return centroid_stamped_perc;
}

/**
 * finding the geometrical center of a given pointcloud
 * @param object_cloud
 * @return
 */
std::vector<geometry_msgs::PoseStamped> findPoses(const std::vector<PointCloudRGBPtr> clouds_in) {
    std::vector<geometry_msgs::PoseStamped> result;

    for (int i = 0; i < clouds_in.size(); i++) {
        geometry_msgs::PoseStamped current_pose;
        PointCloudRGBPtr current_cloud = clouds_in[i];

        ROS_INFO("CALCULATING CENTROID FOR OBJECT %d", i);
        // Calculate centroids
        Eigen::Vector4f centroid;
        pcl::compute3DCentroid(*current_cloud, centroid);
        current_pose.pose.position.x = centroid.x();
        current_pose.pose.position.y = centroid.y();
        current_pose.pose.position.z = centroid.z();

        ROS_INFO("%sCURRENT CLUSTER CENTER\n", "\x1B[32m");
        ROS_INFO("\x1B[32mX: %f\n", current_pose.pose.position.x);
        ROS_INFO("\x1B[32mY: %f\n", current_pose.pose.position.y);
        ROS_INFO("\x1B[32mZ: %f\n", current_pose.pose.position.z);

        // Calculate quaternions
        // TODO: QUATERNIONS!

        // Add header
        current_pose.header.frame_id = "/head_mount_kinect_rgb_optical_frame";

        // Add this PoseStamped to result vector
        result.push_back(current_pose);
    }

    return result;
}

/**
 * estimating surface normals
 * @param input
 * @return
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

    return cloud_normals;
}

/**
 * apply a passthrough Filter to all dimensions, reducing points and
 * narrowing Field of Vision
 * @param input
 * @param x
 * @param y
 * @param z
 * @return
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
        error_message_perc = "Cloud was empty after filtering. ";
        centroid_stamped_perc = findCenterGazebo(); // Use gazebo data instead
    }


    return input_after_xyz;
}

/**
 * estimating plane indices
 * @param input
 * @return
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
        error_message_perc = "No plane found. ";
        centroid_stamped_perc = findCenterGazebo(); // Use gazebo data instead
    }

    return planeIndices;
}

/**
 * extract a pointcloud by indices from an input pointcloud
 * @param input
 * @param indices
 * @param negative
 * @return
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
 * Filtering the input cloud with a moving least squares algorithm
 * @param input
 * @return
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
    ROS_INFO("Finished MLS Filter!");
    return result;
}


/**
 * Filtering the input cloud with a voxel grid
 * @param input
 * @return
 */
PointCloudRGBPtr voxelGridFilter(PointCloudRGBPtr input) {
    PointCloudRGBPtr result(new PointCloudRGB);

    pcl::VoxelGrid<pcl::PointXYZRGB> sor;
    sor.setInputCloud(input);
    sor.setLeafSize(0.005f, 0.005f, 0.005f);
    sor.filter(*result);
    return result;
}

/**
 * removes statistical outliers from pointcloud
 * @param input
 * @return
 */
PointCloudRGBPtr outlierRemoval(PointCloudRGBPtr input) {
    PointCloudRGBPtr cloud_filtered(new PointCloudRGB);
    pcl::StatisticalOutlierRemoval<pcl::PointXYZRGB> sor;
    sor.setInputCloud(input);
    sor.setMeanK(50);
    sor.setStddevMulThresh(2.0);
    sor.filter(*cloud_filtered);

    return cloud_filtered;
}

/**
 * estimate the Features of a pointcloud using VFHSignature308
 * @param input
 * @return
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
    // Set the maximum allowable deviation of the normals,
    // for the region segmentation step.
    cvfh.setEPSAngleThreshold(5.0 / 180.0 * M_PI); // 5 degrees.
    // Set the curvature threshold (maximum disparity between curvatures),
    // for the region segmentation step.
    cvfh.setCurvatureThreshold(1.0);
    // Set to true to normalize the bins of the resulting histogram,
    // using the total number of points. Note: enabling it will make CVFH
    // invariant to scale just like VFH, but the authors encourage the opposite.
    cvfh.setNormalizeBins(false);

    cvfh.compute(*descriptors);

    //float x [308] = descriptors->points[0].histogram; // Save calculated histogram in a float array
    //std::vector<float> result(x, x + sizeof x / sizeof x[0]);
    //std::vector<float> result(std::begin(descriptors->points[0].histogram), std::end(descriptors->points[0].histogram)); // Array to vector

    return descriptors; // to vector
}

/**
 *
 * @param input
 * @return
 */
std::vector<PointCloudRGBPtr> euclideanClusterExtraction(PointCloudRGBPtr input) {
    ROS_INFO("Euclidean Cluster Extraction!");
    pcl::search::KdTree<pcl::PointXYZRGB>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZRGB>);
    tree->setInputCloud(input);

    PointIndicesVector cluster_indices;
    pcl::EuclideanClusterExtraction<pcl::PointXYZRGB> ec;
    ec.setClusterTolerance(0.01); // 4cm
    ec.setMinClusterSize(300);
    ec.setMaxClusterSize(25000);
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
            cloud_cluster->points.push_back(input->points[*pit]); //*
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
 * calculating the alignment of an object to a certain target using sample consensus
 * @param objects
 * @param features
 * @param target
 * @return
 */
PointCloudRGBPtr SACInitialAlignment(std::vector<PointCloudRGBPtr> objects,
                                     std::vector<PointCloudVFHS308Ptr> features,
                                     PointCloudRGBPtr target) {

    PointCloudRGBPtr result(new PointCloudRGB);
    // preprocess cloud
    // apply3DFilter(target, 1.0,1.0,1.0);

    PointCloudRGBPtr temp(new PointCloudRGB);
    temp = target;
    target = voxelGridFilter(temp);

    float fitness_scores[features.size()];
    Eigen::Matrix4f transformation_matrices[features.size()];
    int best_index;



    // calculate inital alignment for every input cloud and save scores
    for (size_t i = 0; i < features.size(); ++i) {
        pcl::SampleConsensusInitialAlignment<pcl::PointXYZRGB, pcl::PointXYZRGB, pcl::VFHSignature308> sac_ia;

        sac_ia.setInputSource(objects[i]);
        sac_ia.setSourceFeatures(features[i]);
        sac_ia.setInputTarget(target);
        // sac_ia.setTargetFeatures(target_features);
        PointCloudRGB registration_output;
        sac_ia.align(registration_output);

        // get fitness score with max squared distance for correspondence
        fitness_scores[i] = (float) sac_ia.getFitnessScore(0.01f * 0.01f);
        transformation_matrices[i] = sac_ia.getFinalTransformation();
    }


    // Find the best template alignment

    // Find the template with the best (lowest) fitness score
    float lowest_score = std::numeric_limits<float>::infinity();
    int best_template = 0;
    for (size_t i = 0; i < sizeof(fitness_scores); ++i) {
        if (fitness_scores[i] < lowest_score) {
            lowest_score = fitness_scores[i];
            best_index = i;
        }
    }

    // Print the alignment fitness score (values less than 0.00002 are good)
    printf("Best fitness score: %f\n", fitness_scores[best_index]);

    // Print the rotation matrix and translation vector
    Eigen::Matrix3f rotation = transformation_matrices[best_index].block<3, 3>(0, 0);
    Eigen::Vector3f translation = transformation_matrices[best_index].block<3, 1>(0, 3);

    // transform with best Transformation

    pcl::transformPointCloud(*objects[best_index], *result, transformation_matrices[best_index]);

    best_ia_index = best_index;

    return result;
}

/**
 * calculating the alignment of an object to a certain target using iterative closest point algorithm
 * @param input
 * @param target
 * @return
 */
PointCloudRGBPtr iterativeClosestPoint(PointCloudRGBPtr input,
                                       PointCloudRGBPtr target) {
    pcl::IterativeClosestPoint<pcl::PointXYZRGB, pcl::PointXYZRGB> icp;
    icp.setInputSource(input);
    icp.setInputTarget(target);
    PointCloudRGBPtr final(new PointCloudRGB);
    icp.align(*final);
    std::cout << "has converged:" << icp.hasConverged() << " score: " <<
              icp.getFitnessScore() << std::endl;
    std::cout << icp.getFinalTransformation() << std::endl;

    return final;
}

/**
 * Color Histogram from RGBA-PointCloud
 * @param input
 * @return concatenated floats (r,g,b (in that order) from Pointcloud-Points
 */
std::vector<uint64_t> produceColorHist(pcl::PointCloud<pcl::PointXYZRGB>::Ptr  cloud){
    int red[8];
    int green[8];
    int blue[8];
    std::vector<uint64_t> result;

    // initialize all array-values with 0
    for (int i = 0; i < 8;i++){
        red[i] = 0;
        green[i] = 0;
        blue[i] = 0;
    }

    for (int i = 0; i <  cloud->size(); i++){
        pcl::PointXYZRGB p = cloud->points[i];
        // increase value in bin at given index
        if (p.r < 32){
            red[0]++;

        } else if (p.r >= 32 && p.r < 64){
            red[1]++;

        } else if (p.r >= 64 && p.r < 96){
            red[2]++;
        } else if (p.r >= 96 && p.r < 128){
            red[3]++;

        } else if (p.r >= 128 && p.r < 160){
            red[4]++;

        } else if (p.r >= 160 && p.r < 192){
            red[5]++;

        } else if (p.r >= 192 && p.r < 224){
            red[6]++;

        } else if (p.r >= 224 && p.r < 256){
            red[7]++;

        }

        if (p.g < 32){
            green[0]++;

        } else if (p.g >= 32 && p.g < 64){
            green[1]++;

        } else if (p.g >= 64 && p.g < 96){
            green[2]++;
        } else if (p.g >= 96 && p.g < 128){
            green[3]++;

        } else if (p.g >= 128 && p.g < 160){
            green[4]++;

        } else if (p.g >= 160 && p.g < 192){
            green[5]++;

        } else if (p.g >= 192 && p.g < 224){
            green[6]++;

        } else if (p.g >= 224 && p.g < 256){
            green[7]++;

        }

        if (p.b < 32){
            blue[0]++;

        } else if (p.b >= 32 && p.b < 64){
            blue[1]++;

        } else if (p.b >= 64 && p.b < 96){
            blue[2]++;
        } else if (p.b >= 96 && p.b < 128){
            blue[3]++;

        } else if (p.b >= 128 && p.b < 160){
            blue[4]++;

        } else if (p.b >= 160 && p.b < 192){
            blue[5]++;

        } else if (p.b >= 192 && p.b < 224){
            blue[6]++;

        } else if (p.b >= 224 && p.b < 256){
            blue[7]++;

        }
    }

    // concatenate red, green and blue entries
    for (int r = 0; r < 8; r++){
        result.push_back(red[r]);
    }
    for (int g = 0; g < 8; g++){
        result.push_back(green[g]);
    }
    for (int b = 0; b < 8; b++){
        result.push_back(blue[b]);
    }

    return result;

}

void getAllFeatures(std::vector<PointCloudRGBPtr> all_clusters, std::vector<float> vfhs_vector,
                    std::vector<uint64_t> color_features_vector) {


    getNormalFeatures(all_clusters, vfhs_vector);
    ROS_INFO("Vision: CVFH filling completed");

    // do the same for the color histogram
    getColorFeatures(all_clusters, color_features_vector);
    ROS_INFO("Vision: Color Histogram filling completed");
}

void getNormalFeatures(std::vector<PointCloudRGBPtr> all_clusters, std::vector<float> current_features_vector) {

    std::vector<PointCloudVFHS308Ptr> vfhs_vector;
    PointCloudVFHS308Ptr vfhs(new pcl::PointCloud<pcl::VFHSignature308>);


    for (int i = 0; i < all_clusters.size(); i++) {

        std::stringstream line;
        line << "object_" << i << "_normal.csv";

        char *line_charp;
        line.str().copy(line_charp, line.str().size(), 0);
        std::ofstream normals_csv_file(line_charp);


        vfhs = cvfhRecognition(all_clusters[i]);
        vfhs_vector.push_back(vfhs);

        for (int x = 0; x < 308; x++) {
            //ROS_INFO("%f", current_features[x]);
            current_features_vector.push_back(vfhs->points[0].histogram[x]);

            if (x == 307) {
                normals_csv_file << vfhs->points[0].histogram[x];
            } else {
                normals_csv_file << vfhs->points[0].histogram[x] << ",";
            }

        }

        normals_csv_file.close();
        normals_csv_file.clear();
        line.clear();


    }
}

void getColorFeatures(std::vector<PointCloudRGBPtr> all_clusters, std::vector<uint64_t> color_features_vector) {

    std::vector<uint64_t> current_color_features;

    for (int i = 0; i < all_clusters.size(); i++) {
        current_color_features = produceColorHist(all_clusters[i]);
        std::stringstream line;
        line << "object_" << i << "_color.csv";

        char *line_charp;
        line.str().copy(line_charp, line.str().size(), 0);
        std::ofstream color_csv_file(line_charp);

        for (int x = 0; x < 24; x++) {
            //ROS_INFO("%f", current_color_features[x]);
            color_features_vector.push_back(current_color_features[x]);

            if (x == 23) {
                color_csv_file << current_color_features[x];
            } else {
                color_csv_file << current_color_features[x] << ",";
            }

        }
        color_csv_file.close();
        color_csv_file.clear();
        line.clear();

    }
}