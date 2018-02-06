#include "perception.h"

geometry_msgs::PointStamped centroid_stamped_perc;

std::string error_message_perc;

PointCloudRGBPtr cloud_plane(new PointCloudRGB),
        cloud_cluster(new PointCloudRGB),
        cloud_cluster2(new PointCloudRGB),
        cloud_f(new PointCloudRGB),
        cloud_3df(new PointCloudRGB),
        cloud_voxelgridf(new PointCloudRGB),
        cloud_mlsf(new PointCloudRGB),
        cloud_prism(new PointCloudRGB),
        cloud_final(new PointCloudRGB);

/**
 * Find the object!
 * @param kinect
 */
std::vector<PointCloudRGBPtr> findCluster(PointCloudRGBPtr kinect) {

    ros::NodeHandle n;
    std::vector<PointCloudRGBPtr> result;

    CloudTransformer transform_cloud(n);

    // the 'f' in the identifier stands for filtered




    PointIndices plane_indices(new pcl::PointIndices), plane_indices2(new pcl::PointIndices), prism_indices(
            new pcl::PointIndices);


    if (kinect->points.size() < 500)                        // if PR2 is not looking at anything
    {
        ROS_ERROR("Input from kinect is empty");
        error_message_perc = "Cloud empty. ";
        centroid_stamped_perc = findCenterGazebo();         // Use gazebo data instead
    } else {
        ROS_INFO("Starting Cluster extraction");

        cloud_3df =  apply3DFilter(kinect, 0.4, 0.4, 1.5);   // passthrough filter
        // std::cout << "after 3dfilter cluster is of size: " << cloud_3df->size() << std::endl;
        savePointCloudRGBNamed(cloud_3df, "3df");
        cloud_voxelgridf = voxelGridFilter(cloud_3df);      // voxel grid filter
        savePointCloudRGBNamed(cloud_voxelgridf, "voxelgrid");
        // std::cout << "after vgfilter cluster is of size: " << cloud_voxelgridf->size() << std::endl;
        cloud_mlsf = mlsFilter(cloud_voxelgridf);           // moving least square filter
        savePointCloudRGBNamed(cloud_mlsf, "mlsf");
        // std::cout << "after mlsfilter cluster is of size: " << cloud_mlsf->size() << std::endl;
        cloud_cluster2 = cloud_mlsf; // cloud_f set after last filtering function is applied

        cloud_cluster2 = transform_cloud.removeBelowPlane(cloud_cluster2);
        cloud_cluster = cloud_cluster2;
        // std::cout << "cluster after removeBelowPlane is of size: " << cloud_cluster->size() << std::endl;

        // TODO: Irgendwas ab hier vernichtet Vorderseiten der Objekte D:

        // While a segmented plane would be larger than 500 points, segment it.
        bool loop_plane_segmentations = true;
        int amount_plane_segmentations = 0;
        while (loop_plane_segmentations) {
            plane_indices = estimatePlaneIndices(cloud_cluster);
            if (plane_indices->indices.size() > 500)         // is the extracted plane big enough?
            {
                ROS_INFO("plane_indices: %lu", plane_indices->indices.size());
                ROS_INFO("cloud_cluster: %lu", cloud_cluster->points.size());
                cloud_cluster = extractCluster(cloud_cluster, plane_indices, true); // actually extract the object
                amount_plane_segmentations++;
            } else loop_plane_segmentations = false;          // if not big enough, stop looping.
        }
        ROS_INFO("Extracted %d planes!", amount_plane_segmentations);

        cloud_final = outlierRemoval(cloud_cluster);

        // Split cloud_final into one PointCloud per object
        PointIndicesVectorPtr cluster_indices = euclideanClusterExtraction(cloud_final);
        for(int i = 0; i < cluster_indices.size(); i++){
            result.push_back(extractCluster(cloud_final, cluster_indices[i], false));
        }


        if (cloud_final->points.size() == 0) {
            ROS_ERROR("Extracted Cluster is empty");
            error_message_perc = "Final extracted cluster was empty. ";
            centroid_stamped_perc = findCenterGazebo(); // Use gazebo data instead
        }
        else{
            ROS_INFO("%sExtraction OK", "\x1B[32m");
        }

        error_message_perc = "";
        savePointCloudRGBNamed(kinect, "unprocessed");
        savePointCloudRGBNamed(cloud_cluster2, "removedBelowPlane");
        savePointCloudRGBNamed(cloud_cluster, "final_with_outliers");
        savePointCloudRGBNamed(cloud_final, "final");

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
std::vector<geometry_msgs::PointStamped> findCenter(const std::vector<SMSGSPointCloud2> object_clouds_in) {
    std::vector<geometry_msgs::PointStamped> result;

    // convert pointclouds
    std::vector<PointCloudRGBPtr> object_clouds;
    PointCloudRGBPtr temp(new PointCloudRGB);
    for (int i = 0; i < object_clouds_in.size(); i++) {
        pcl::fromROSMsg(object_clouds_in[i], *temp);
        object_clouds[i] = temp;
    }

    // calculate centroids

    PointCloudRGBPtr object_cloud = object_clouds[0];
        if (object_cloud->points.size() != 0) {
            int cloud_size = object_cloud->points.size();

            Eigen::Vector4f centroid;

            pcl::compute3DCentroid(*object_cloud, centroid);

            centroid_stamped_perc.point.x = centroid.x();
            centroid_stamped_perc.point.y = centroid.y();
            centroid_stamped_perc.point.z = centroid.z();

            ROS_INFO("%sCURRENT CLUSTER CENTER\n", "\x1B[32m");
            ROS_INFO("\x1B[32mX: %f\n", centroid_stamped_perc.point.x);
            ROS_INFO("\x1B[32mY: %f\n", centroid_stamped_perc.point.y);
            ROS_INFO("\x1B[32mZ: %f\n", centroid_stamped_perc.point.z);
            centroid_stamped_perc.header.frame_id = "/head_mount_kinect_ir_optical_frame";

            result.push_back(centroid_stamped_perc);
        } else {
            ROS_ERROR("CLOUD EMPTY. NO POINT EXTRACTED");
        }

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
    //segmentation.setModelType(pcl::SACMODEL_PERPENDICULAR_PLANE); // PERPENDICULAR
    segmentation.setModelType(pcl::SACMODEL_PLANE);
    segmentation.setMethodType(pcl::SAC_RANSAC);
    //segmentation.setMaxIterations(500); // Default is 50 and could be problematic
    //segmentation.setAxis(Eigen::Vector3f(0,1,0));
    //segmentation.setEpsAngle(30.0); // plane can be within 30 degrees of X-Z plane - 30.0f * (M_PI/180.0f)
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
    PointCloudRGBPtr result (new PointCloudRGB);

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
    PointCloudRGBPtr result (new PointCloudRGB);

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
    sor.setLeafSize(0.01f, 0.01f, 0.01f);
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
    sor.setMeanK(25);
    sor.setStddevMulThresh(1.0);
    sor.filter(*cloud_filtered);

    return cloud_filtered;
}

/**
 * estimate the Features of a pointcloud using VFHSignature308
 * @param input
 * @return
 */
 std::vector<float> cvfhRecognition(PointCloudRGBPtr input) {
    ROS_INFO("CVFH Recognition!");
    std::vector<float> result;
    // Object for storing the normals.
    pcl::PointCloud<pcl::Normal>::Ptr normals(new pcl::PointCloud<pcl::Normal>);
    // Object for storing the CVFH descriptors.
    pcl::PointCloud<pcl::VFHSignature308>::Ptr descriptors(new pcl::PointCloud<pcl::VFHSignature308>);

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
    for (size_t i = 0; i < 308; i++){
        result.push_back(descriptors->points[0].histogram[i]);
    }

    return result; // to vector
}

PointIndicesVectorPtr euclideanClusterExtraction(PointCloudRGBPtr input){
    ROS_INFO("Euclidean Cluster Extraction!");
    pcl::search::KdTree<pcl::PointXYZRGB>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZRGB>);
    tree->setInputCloud (input);

    PointIndicesVector cluster_indices;
    pcl::EuclideanClusterExtraction<pcl::PointXYZRGB> ec;
    ec.setClusterTolerance (0.04); // 4cm
    ec.setMinClusterSize (50);
    ec.setMaxClusterSize (25000);
    ec.setSearchMethod (tree);
    ec.setInputCloud (input);
    ROS_INFO("BEFORE EXTRACT");
    ec.extract (cluster_indices);

    ROS_INFO("AFTER EXTRACT");
    // PointIndicesVector to PointIndicesVectorPtr
    PointIndicesVectorPtr cluster_indices_ptr;
    pcl::PointIndices::Ptr tmp_indices (new pcl::PointIndices);
    for(int n = 0; n < cluster_indices.size(); n++){
        *tmp_indices = cluster_indices[n];
        cluster_indices_ptr.push_back(tmp_indices);
    }

    ROS_INFO("Finished Euclidean Cluster Extraction!");
    return cluster_indices_ptr;
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

    PointCloudRGBPtr result (new PointCloudRGB);
    // preprocess cloud
    // apply3DFilter(target, 1.0,1.0,1.0);

    PointCloudRGBPtr temp (new PointCloudRGB);
    temp = target;
    target = voxelGridFilter(temp);

    float fitness_scores[features.size()];
    Eigen::Matrix4f transformation_matrices[features.size()];
    int best_index;



    // calculate inital alignment for every input cloud and save scores
    for (size_t i = 0; i < features.size (); ++i)
    {
        pcl::SampleConsensusInitialAlignment<pcl::PointXYZRGB, pcl::PointXYZRGB, pcl::VFHSignature308> sac_ia;

        sac_ia.setInputCloud (objects[i]);
        sac_ia.setSourceFeatures (features[i]);
        sac_ia.setInputTarget(target);
        // sac_ia.setTargetFeatures(target_features);
        PointCloudRGB registration_output;
        sac_ia.align (registration_output);

        // get fitness score with max squared distance for correspondence
        fitness_scores[i] = (float) sac_ia.getFitnessScore (0.01f*0.01f);
        transformation_matrices[i] = sac_ia.getFinalTransformation ();
    }


    // Find the best template alignment

    // Find the template with the best (lowest) fitness score
    float lowest_score = std::numeric_limits<float>::infinity ();
    int best_template = 0;
    for (size_t i = 0; i < sizeof(fitness_scores); ++i)
    {
        if (fitness_scores[i] < lowest_score)
        {
            lowest_score = fitness_scores[i];
            best_index =  i;
        }
    }

    // Print the alignment fitness score (values less than 0.00002 are good)
    printf ("Best fitness score: %f\n", fitness_scores[best_index]);

    // Print the rotation matrix and translation vector
    Eigen::Matrix3f rotation = transformation_matrices[best_index].block<3,3>(0, 0);
    Eigen::Vector3f translation =  transformation_matrices[best_index].block<3,1>(0, 3);

    // transform with best Transformation

    pcl::transformPointCloud(*objects[best_index], *result, transformation_matrices[best_index]);

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
    icp.setInputCloud(input);
    icp.setInputTarget(target);
    PointCloudRGBPtr final (new PointCloudRGB);
    icp.align(*final);
    std::cout << "has converged:" << icp.hasConverged() << " score: " <<
              icp.getFitnessScore() << std::endl;
    std::cout << icp.getFinalTransformation() << std::endl;

    return final;
}

/**
 * Color Histogram from RGB-PointCloud
 * @param input
 * @return concatenated floats (r,g,b (in that order) from Pointcloud-Points
 */
std::vector<float> produceColorHist(PointCloudRGBPtr input){

    input->resize(500); // resize for vector messages

    std::vector<float> red;
    std::vector<float> green;
    std::vector<float> blue;
    std::vector<float> result;

    for (size_t i = 0; i < input->size(); i++){
        red.push_back(input->points.at(i).r);
        green.push_back(input->points.at(i).g);
        blue.push_back(input->points.at(i).b);

    }

    // concatenate red, green and blue entries
    result.insert(result.begin(),red.begin(),red.end());
    result.insert(result.end(),green.begin(),green.end());
    result.insert(result.end(),blue.begin(),blue.end());


    return result;

}