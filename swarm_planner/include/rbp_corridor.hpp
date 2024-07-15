#pragma once

#include <Eigen/Dense>

#include <init_traj_planner.hpp>
#include <mission.hpp>
#include <param.hpp>
#include <timer.hpp>

namespace SwarmPlanning {
    class Corridor {
    public:
        Corridor(std::shared_ptr<DynamicEDTOctomap> _distmap_obj,
                 Mission _mission,
                 Param _param)
                : distmap_obj(std::move(_distmap_obj)),
                  mission(std::move(_mission)),
                  param(std::move(_param)) {
        }

        bool update(bool _log, SwarmPlanning::PlanResult* _planResult_ptr) {
            log = _log;
            planResult_ptr = _planResult_ptr;
            makespan = planResult_ptr->T.back();
            return updateObsBox() && updateRelBox();
        }

        bool update_flat_box(bool _log, SwarmPlanning::PlanResult* _planResult_ptr) {
            log = _log;
            planResult_ptr = _planResult_ptr;
            makespan = planResult_ptr->T.size() - 1;
            return updateFlatObsBox() && updateFlatRelBox() && updateTs();
        }

    private:
        std::shared_ptr<DynamicEDTOctomap> distmap_obj;
        Mission mission;
        Param param;

        bool log;
        SwarmPlanning::PlanResult* planResult_ptr;
        double makespan;

        bool isObstacleInBox(const std::vector<double> &box, double margin) {
            double x, y, z;
            int count1 = 0;
            for (double i = box[0]; i < box[3] + SP_EPSILON_FLOAT; i += param.box_xy_res) {
                int count2 = 0;
                for (double j = box[1]; j < box[4] + SP_EPSILON_FLOAT; j += param.box_xy_res) {
                    int count3 = 0;
                    for (double k = box[2]; k < box[5] + SP_EPSILON_FLOAT; k += param.box_z_res) {
                        x = i + SP_EPSILON_FLOAT;
                        if (count1 == 0 && box[0] > param.world_x_min + SP_EPSILON_FLOAT) {
                            x = box[0] - SP_EPSILON_FLOAT;
                        }
                        y = j + SP_EPSILON_FLOAT;
                        if (count2 == 0 && box[1] > param.world_y_min + SP_EPSILON_FLOAT) {
                            y = box[1] - SP_EPSILON_FLOAT;
                        }
                        z = k + SP_EPSILON_FLOAT;
                        if (count3 == 0 && box[2] > param.world_z_min + SP_EPSILON_FLOAT) {
                            z = box[2] - SP_EPSILON_FLOAT;
                        }

                        octomap::point3d cur_point(x, y, z);
                        float dist = distmap_obj.get()->getDistance(cur_point);
                        if (dist < margin - SP_EPSILON_FLOAT) {
                            return true;
                        }
                        count3++;
                    }
                    count2++;
                }
                count1++;
            }

            return false;
        }

        bool isBoxInBoundary(const std::vector<double> &box) {
            return box[0] > param.world_x_min - SP_EPSILON &&
                   box[1] > param.world_y_min - SP_EPSILON &&
                   box[2] > param.world_z_min - SP_EPSILON &&
                   box[3] < param.world_x_max + SP_EPSILON &&
                   box[4] < param.world_y_max + SP_EPSILON &&
                   box[5] < param.world_z_max + SP_EPSILON;
        }

        bool isPointInBox(const octomap::point3d &point,
                          const std::vector<double> &box) {
            return point.x() > box[0] - SP_EPSILON &&
                   point.y() > box[1] - SP_EPSILON &&
                   point.z() > box[2] - SP_EPSILON &&
                   point.x() < box[3] + SP_EPSILON &&
                   point.y() < box[4] + SP_EPSILON &&
                   point.z() < box[5] + SP_EPSILON;
        }

        void expand_box(std::vector<double> &box, double margin) {
            // 存储候选盒子和更新后的盒子
            std::vector<double> box_cand, box_update;
            std::vector<int> axis_cand{0, 1, 2, 3, 4, 5};

            int i = -1;
            int axis;
            while (!axis_cand.empty()) {
                box_cand = box;
                box_update = box;

                //check update_box only! update_box + current_box = cand_box
                // 该循环会一直拓展, 直到碰到障碍物为止
                while (!isObstacleInBox(box_update, margin) && isBoxInBoundary(box_update)) {
                    i++;
                    if (i >= axis_cand.size()) {
                        i = 0;
                    }
                    // 通过索引选择要扩展的轴
                    axis = axis_cand[i];

                    //update current box
                    box = box_cand;
                    box_update = box_cand;

                    //expand cand_box and get updated part of box(update_box)
                    if (axis < 3) {
                        box_update[axis + 3] = box_cand[axis];
                        if (axis == 2) {
                            box_cand[axis] = box_cand[axis] - param.box_z_res;
                        } else {
                            box_cand[axis] = box_cand[axis] - param.box_xy_res;
                        }
                        box_update[axis] = box_cand[axis];
                    } else {
                        box_update[axis - 3] = box_cand[axis];
                        if (axis == 5) {
                            box_cand[axis] = box_cand[axis] + param.box_z_res;
                        } else {
                            box_cand[axis] = box_cand[axis] + param.box_xy_res;
                        }
                        box_update[axis] = box_cand[axis];
                    }
                }
                axis_cand.erase(axis_cand.begin() + i);
                if (i > 0) {
                    i--;
                } else {
                    i = axis_cand.size() - 1;
                }
            }
        }

        bool updateObsBox() {
            double x_next, y_next, z_next, dx, dy, dz;
            Timer timer;

            planResult_ptr->SFC.resize(mission.qn);
            for (size_t qi = 0; qi < mission.qn; ++qi) {
                std::vector<double> box_prev{0, 0, 0, 0, 0, 0};

                for (int i = 0; i < planResult_ptr->initTraj[qi].size() - 1; i++) {
                    auto state = planResult_ptr->initTraj[qi][i];
                    double x = state.x();
                    double y = state.y();
                    double z = state.z();

                    std::vector<double> box;
                    auto state_next = planResult_ptr->initTraj[qi][i + 1];
                    x_next = state_next.x();
                    y_next = state_next.y();
                    z_next = state_next.z();

                    if (isPointInBox(octomap::point3d(x_next, y_next, z_next), box_prev)) {
                        continue;
                    }

                    // Initialize box
                    box.emplace_back(round(std::min(x, x_next) / param.box_xy_res) * param.box_xy_res);
                    box.emplace_back(round(std::min(y, y_next) / param.box_xy_res) * param.box_xy_res);
                    box.emplace_back(round(std::min(z, z_next) / param.box_z_res) * param.box_z_res);
                    box.emplace_back(round(std::max(x, x_next) / param.box_xy_res) * param.box_xy_res);
                    box.emplace_back(round(std::max(y, y_next) / param.box_xy_res) * param.box_xy_res);
                    box.emplace_back(round(std::max(z, z_next) / param.box_z_res) * param.box_z_res);

                    if (isObstacleInBox(box, mission.quad_size[qi])) {
                        ROS_ERROR("Corridor: Invalid initial trajectory. Obstacle invades initial trajectory.");
                        ROS_ERROR_STREAM("Corridor: x " << x << ", y " << y << ", z " << z);

                        bool debug =isObstacleInBox(box, mission.quad_size[qi]);
                        return false;
                    }
                    expand_box(box, mission.quad_size[qi]);

                    planResult_ptr->SFC[qi].emplace_back(std::make_pair(box, -1));

                    box_prev = box;
                }

                // Generate box time segment
                int box_max = planResult_ptr->SFC[qi].size();
                int path_max = planResult_ptr->initTraj[qi].size();
                Eigen::MatrixXd box_log = Eigen::MatrixXd::Zero(box_max, path_max);

                for (int i = 0; i < box_max; i++) {
                    for (int j = 0; j < path_max; j++) {
                        if (isPointInBox(planResult_ptr->initTraj[qi][j], planResult_ptr->SFC[qi][i].first)) {
                            if (j == 0) {
                                box_log(i, j) = 1;
                            } else {
                                box_log(i, j) = box_log(i, j - 1) + 1;
                            }
                        }
                    }
                }

                int box_iter = 0;
                for (int path_iter = 0; path_iter < path_max; path_iter++) {
                    if (box_iter == box_max - 1) {
                        if (box_log(box_iter, path_iter) > 0) {
                            continue;
                        } else {
                            box_iter--;
                        }
                    }
                    if (box_log(box_iter, path_iter) > 0 && box_log(box_iter + 1, path_iter) > 0) {
                        int count = 1;
                        while (path_iter + count < path_max && box_log(box_iter, path_iter + count) > 0
                               && box_log(box_iter + 1, path_iter + count) > 0) {
                            count++;
                        }
                        int obs_index = path_iter + count / 2;
                        planResult_ptr->SFC[qi][box_iter].second = planResult_ptr->T[obs_index];

                        path_iter = path_iter + count / 2;
                        box_iter++;
                    } else if (box_log(box_iter, path_iter) == 0) {
                        box_iter--;
                        path_iter--;
                    }
                }
                planResult_ptr->SFC[qi][box_max - 1].second = makespan;
            }

            timer.stop();
            ROS_INFO_STREAM("Corridor: SFC runtime=" << timer.elapsedSeconds());
            return true;
        }

        bool updateObsBox_seperate() {
            double x_next, y_next, z_next, dx, dy, dz;
            Timer timer;

            planResult_ptr->SFC.resize(mission.qn);
            for (size_t qi = 0; qi < mission.qn; ++qi) {
                std::vector<double> box_prev{0, 0, 0, 0, 0, 0};

                for (int i = 0; i < planResult_ptr->initTraj[qi].size() - 1; i++) {
                    auto state = planResult_ptr->initTraj[qi][i];
                    double x = state.x();
                    double y = state.y();
                    double z = state.z();

                    std::vector<double> box;
                    auto state_next = planResult_ptr->initTraj[qi][i + 1];
                    x_next = state_next.x();
                    y_next = state_next.y();
                    z_next = state_next.z();

                    if (isPointInBox(octomap::point3d(x_next, y_next, z_next), box_prev)) {
                        continue;
                    }

                    // Initialize box
                    box.emplace_back(round(std::min(x, x_next) / param.box_xy_res) * param.box_xy_res);
                    box.emplace_back(round(std::min(y, y_next) / param.box_xy_res) * param.box_xy_res);
                    box.emplace_back(round(std::min(z, z_next) / param.box_z_res) * param.box_z_res);
                    box.emplace_back(round(std::max(x, x_next) / param.box_xy_res) * param.box_xy_res);
                    box.emplace_back(round(std::max(y, y_next) / param.box_xy_res) * param.box_xy_res);
                    box.emplace_back(round(std::max(z, z_next) / param.box_z_res) * param.box_z_res);

                    if (isObstacleInBox(box, mission.quad_size[qi])) {
                        ROS_ERROR("Corridor: Invalid initial trajectory. Obstacle invades initial trajectory.");
                        return false;
                    }
                    expand_box(box, mission.quad_size[qi]);

                    planResult_ptr->SFC[qi].emplace_back(std::make_pair(box, -1));

                    box_prev = box;
                }

                // Generate box time segment
                int box_max = planResult_ptr->SFC[qi].size();
                int path_max = planResult_ptr->initTraj[qi].size();
                Eigen::MatrixXd box_log = Eigen::MatrixXd::Zero(box_max, path_max);

                for (int i = 0; i < box_max; i++) {
                    for (int j = 0; j < path_max; j++) {
                        if (isPointInBox(planResult_ptr->initTraj[qi][j], planResult_ptr->SFC[qi][i].first)) {
                            if (j == 0) {
                                box_log(i, j) = 1;
                            } else {
                                box_log(i, j) = box_log(i, j - 1) + 1;
                            }
                        }
                    }
                }

                int box_iter = 0;
                for (int path_iter = 0; path_iter < path_max; path_iter++) {
                    if (box_iter == box_max - 1) {
                        if (box_log(box_iter, path_iter) > 0) {
                            continue;
                        } else {
                            box_iter--;
                        }
                    }
                    if (box_log(box_iter, path_iter) > 0 && box_log(box_iter + 1, path_iter) > 0) {
                        int count = 1;
                        while (path_iter + count < path_max && box_log(box_iter, path_iter + count) > 0
                               && box_log(box_iter + 1, path_iter + count) > 0) {
                            count++;
                        }
                        int obs_index = path_iter + count / 2;
                        planResult_ptr->SFC[qi][box_iter].second = planResult_ptr->T[obs_index];

                        path_iter = path_iter + count / 2;
                        box_iter++;
                    } else if (box_log(box_iter, path_iter) == 0) {
                        box_iter--;
                        path_iter--;
                    }
                }
                planResult_ptr->SFC[qi][box_max - 1].second = makespan;
            }

            timer.stop();
            ROS_INFO_STREAM("Corridor: SFC runtime=" << timer.elapsedSeconds());
            return true;
        }

        bool updateRelBox() {
            Timer timer;

            planResult_ptr->RSFC.resize(mission.qn);
            for (int qi = 0; qi < mission.qn; qi++) {
                planResult_ptr->RSFC[qi].resize(mission.qn);
                for (int qj = qi + 1; qj < mission.qn; qj++) {
                    int path_size = planResult_ptr->initTraj[qi].size();
                    if (planResult_ptr->initTraj[qi].size() != planResult_ptr->initTraj[qj].size()) {
                        ROS_ERROR("Corridor: size of initial trajectories must be equal");
                        return false;
                    }

                    octomap::point3d a, b, c, n, m;
                    double dist, dist_min;
                    for (int iter = 1; iter < planResult_ptr->T.size(); iter++) {
                        a = planResult_ptr->initTraj[qj][iter - 1] - planResult_ptr->initTraj[qi][iter - 1];
                        b = planResult_ptr->initTraj[qj][iter] - planResult_ptr->initTraj[qi][iter];

                        // Coordinate transformation
                        a.z() = a.z() / param.downwash;
                        b.z() = b.z() / param.downwash;

                        // get closest point of L from origin
                        if (a == b) {
                            m = a;
                        } else {
                            m = a;
                            dist_min = a.norm();

                            dist = b.norm();
                            if (dist_min > dist) {
                                m = b;
                                dist_min = dist;
                            }

                            n = b - a;
                            n.normalize();
                            c = a - n * a.dot(n);
                            dist = c.norm();
                            if ((c - a).dot(c - b) < 0 && dist_min > dist) {
                                m = c;
                            }
                        }
                        m.normalize();

                        m.z() = m.z() / param.downwash;
                        if (m.norm() == 0) {
                            ROS_ERROR("Corridor: initial trajectories are collided with each other");
                            return false;
                        }

                        planResult_ptr->RSFC[qi][qj].emplace_back(std::make_pair(m, planResult_ptr->T[iter]));
                    }
                }
            }

            timer.stop();
            ROS_INFO_STREAM("Corridor: RSFC runtime=" << timer.elapsedSeconds());
            return true;
        }

        bool updateFlatObsBox() {
            double x_next, y_next, z_next, dx, dy, dz;

            Timer timer;

            planResult_ptr->SFC.resize(mission.qn);
            // 遍历所有任务
            for (size_t qi = 0; qi < mission.qn; ++qi) {
                // 用于存储上一个盒子的状态, 初始化为0
                std::vector<double> box_prev;
                for (int i = 0; i < 6; i++) box_prev.emplace_back(0);
                // 遍历每个任务的初始轨迹, 除了最后一个点
                for (int i = 0; i < planResult_ptr->initTraj[qi].size() - 1; i++) {
                    auto state = planResult_ptr->initTraj[qi][i];
                    // 获得当前轨迹的三维坐标
                    double x = state.x();
                    double y = state.y();
                    double z = state.z();

                    std::vector<double> box;
                    auto state_next = planResult_ptr->initTraj[qi][i + 1];
                    // 获取下一个轨迹的三维坐标
                    x_next = state_next.x();
                    y_next = state_next.y();
                    z_next = state_next.z();
                    // 检查下一个轨迹点是否已经在之前的box中
                    if (isPointInBox(octomap::point3d(x_next, y_next, z_next), box_prev)) {
                        continue;
                    }

                    // Initialize box
                    box.emplace_back(std::min(x, x_next) - param.box_xy_res / 2.0);
                    box.emplace_back(std::min(y, y_next) - param.box_xy_res / 2.0);
                    box.emplace_back(std::min(z, z_next) - param.box_z_res / 2.0);
                    box.emplace_back(std::max(x, x_next) + param.box_xy_res / 2.0);
                    box.emplace_back(std::max(y, y_next) + param.box_xy_res / 2.0);
                    box.emplace_back(std::max(z, z_next) + param.box_z_res / 2.0);


                    if (isObstacleInBox(box, mission.quad_size[qi])) {
                        ROS_ERROR("Corridor: Invalid initial trajectory, obstacle invades initial trajectory.");
                        return false;
                    }
                    expand_box(box, mission.quad_size[qi]);

                    planResult_ptr->SFC[qi].emplace_back(std::make_pair(box, -1));

                    box_prev = box;
                }

                // Generate box time segment
                int box_max = planResult_ptr->SFC[qi].size();
                int path_max = planResult_ptr->initTraj[qi].size();
                // 记录每个盒子与轨迹点之间的关系
                Eigen::MatrixXd box_log = Eigen::MatrixXd::Zero(box_max, path_max);
                // 用于存储时间戳
                std::vector<int> ts;
                // 填充 box_log, 用于调试
                for (int i = 0; i < box_max; i++) {
                    for (int j = 0; j < path_max; j++) {
                        if (isPointInBox(planResult_ptr->initTraj[qi][j], planResult_ptr->SFC[qi][i].first)) {
                            if (j == 0) {
                                box_log(i, j) = 1;
                            } else {
                                box_log(i, j) = box_log(i, j - 1) + 1;
                            }
                        }
                    }
                }

                if (log) {
                    std::cout << qi << std::endl;
                    std::cout << box_log << std::endl;
                }

                int box_iter = 0;
                // 循环遍历所有的轨迹点
                for (int path_iter = 0; path_iter < path_max; path_iter++) {
                    // 如果达到最后一个盒子, 终止循环
                    if (box_iter >= box_max - 1) {
                        break;
                    }
                    // 检查当前盒子和下一个盒子是否都包含当前的轨迹点 path_iter
                    if (box_log(box_iter, path_iter) > 0 && box_log(box_iter + 1, path_iter) > 0) {
                        int count = 1;
                        while (path_iter + count < path_max && box_log(box_iter, path_iter + count) > 0
                               && box_log(box_iter + 1, path_iter + count) > 0) {
                            count++;
                        }
                        double obs_index = path_iter + count / 2;
                        // 为每个盒子的中心点分配时间戳
                        planResult_ptr->SFC[qi][box_iter].second = obs_index * param.time_step;
                        planResult_ptr->T.emplace_back(obs_index);

                        path_iter = path_iter + count / 2;
                        box_iter++;
                    }
                }
                // 确保最后一个盒子的时间戳设置为整个路径规划的总时间长度, 这样可以保证路径规划在预定的时间内完成.
                planResult_ptr->SFC[qi][box_max - 1].second = makespan * param.time_step;
            }

            timer.stop();
            ROS_INFO_STREAM("Corridor: SFC runtime=" << timer.elapsedSeconds());
            return true;
        }

        bool updateFlatRelBox() {
            int sector_range[6] = {-3, -2, -1, 1, 2, 3};

            Timer timer;

            planResult_ptr->RSFC.resize(mission.qn);
            for (int qi = 0; qi < mission.qn; qi++) {
                planResult_ptr->RSFC[qi].resize(mission.qn);
                for (int qj = qi + 1; qj < mission.qn; qj++) {
                    // 计算两个无人机初始轨迹的最大和最小长度
                    int path_max = std::max<int>(planResult_ptr->initTraj[qi].size(),
                                                 planResult_ptr->initTraj[qj].size());
                    int path_min = std::min<int>(planResult_ptr->initTraj[qi].size(),
                                                 planResult_ptr->initTraj[qj].size());
                    Eigen::MatrixXd sector_log = Eigen::MatrixXd::Zero(6, path_max);
                    // 遍历每个轨迹点
                    for (int iter = 0; iter < path_max; iter++) {
                        // Get rel_pose
                        int rel_pose[4];
                        double dx, dy, dz;

                        if (iter < path_min) {
                            dx = round((planResult_ptr->initTraj[qj][iter].x()
                                    - planResult_ptr->initTraj[qi][iter].x()) / param.grid_xy_res);
                            dy = round((planResult_ptr->initTraj[qj][iter].y()
                                    - planResult_ptr->initTraj[qi][iter].y()) / param.grid_xy_res);
                            dz = round((planResult_ptr->initTraj[qj][iter].z()
                                    - planResult_ptr->initTraj[qi][iter].z()) / param.grid_z_res);
                        } else if (planResult_ptr->initTraj[qi].size() == path_min) {
                            dx = round((planResult_ptr->initTraj[qj][iter].x()
                                    - planResult_ptr->initTraj[qi][path_min - 1].x()) / param.grid_xy_res);
                            dy = round((planResult_ptr->initTraj[qj][iter].y()
                                    - planResult_ptr->initTraj[qi][path_min - 1].y()) / param.grid_xy_res);
                            dz = round((planResult_ptr->initTraj[qj][iter].z()
                                    - planResult_ptr->initTraj[qi][path_min - 1].z()) / param.grid_z_res);
                        } else {
                            dx = round((planResult_ptr->initTraj[qj][path_min - 1].x()
                                    - planResult_ptr->initTraj[qi][iter].x()) / param.grid_xy_res);
                            dy = round((planResult_ptr->initTraj[qj][path_min - 1].y()
                                    - planResult_ptr->initTraj[qi][iter].y()) / param.grid_xy_res);
                            dz = round((planResult_ptr->initTraj[qj][path_min - 1].z()
                                    - planResult_ptr->initTraj[qi][iter].z()) / param.grid_z_res);
                        }
                        // Caution: (q1_size+q2_size)/grid_size should be small enough!
                        // 存储符号信息, 用于确定两架无人机在各个轴向上的相对朝向
                        // 判断j的位置 with respect to i
                        rel_pose[1] = (dx > SP_EPSILON_FLOAT) - (dx < -SP_EPSILON_FLOAT);
                        rel_pose[2] = (dy > SP_EPSILON_FLOAT) - (dy < -SP_EPSILON_FLOAT);
                        rel_pose[3] = (dz > SP_EPSILON_FLOAT) - (dz < -SP_EPSILON_FLOAT);

                        // Save sector information
                        for (int i = 0; i < 6; i++) {
                            int sector = sector_range[i];
                            int sgn = (i > 2) - (i < 3);
                            if (rel_pose[abs(sector)] * sgn > 0) {
                                if (iter == 0) {
                                    sector_log(i, iter) = 1;
                                } else {
                                    // 如果无人机从一个轨迹点移动到下一个轨迹点而没有离开扇区 i, 那么这个扇区的计数
                                    sector_log(i, iter) = sector_log(i, iter - 1) + 1;
                                }
                            }
                        }
                    }

                    //find minimum jump sector path (heuristic greedy search)
                    // 从轨迹最后一个点前向搜索
                    int iter = path_max - 1;
                    // 存储下一个扇区的索引
                    int sector_next = -1;
                    // 存储该扇区覆盖的轨迹点数
                    int count_next = sector_log.col(iter).maxCoeff(&sector_next);

                    planResult_ptr->RSFC[qi][qj].emplace_back(
                            std::make_pair(sec2normVec(sector_range[sector_next]), makespan * param.time_step));
                    // 跳过当前扇区覆盖的轨迹点
                    iter = iter - count_next + 1;

                    while (iter > 1) {
                        int sector_curr;
                        int count;

                        // if there is no intersection then allow to jump sector
                        // 构建最小跳跃扇区路径
                        // except jumping through quadrotor (i.e. +x -> -x jumping is not allowed)
                        // 表示轨迹点 iter处有跳跃
                        if (sector_log.col(iter).maxCoeff(&sector_curr) <= 1) {
                            // 找到轨迹点
                            iter = iter - 1;
                            // 计算与下一个扇区相反的方向的扇区
                            int sector_opp = 6 - 1 - sector_next;

                            if (sector_log.col(iter).maxCoeff(&sector_curr) <= 0) {
                                ROS_ERROR("Corridor: Invalid initial trajectory, there is missing link");
                                std::cout << sector_log << std::endl;
                                return false;
                            } else if (sector_curr == sector_opp) {
                                bool flag = false;
                                for (int i = 0; i < 6; i++) {
                                    // 检查是否有合法扇区
                                    if (i != sector_opp &&
                                        sector_log(i, iter) == sector_log.col(iter).maxCoeff(&sector_curr)) {
                                        flag = true;
                                        break;
                                    }
                                }
                                if (!flag) {
                                    ROS_ERROR("Corridor: Invalid Path, jumping through quadrotor");
                                    std::cout << sector_log << std::endl;
                                    return false;
                                }
                            }
                            count = 0;
                        } else {
                            count = 1;
                            // search for the middle waypoint among the intersection of two sequential convex sets
                            while (sector_log(sector_curr, iter + count) > 0) {
                                count++;
                            }
                        }

                        double rel_index;
                        if (count == 0) {
                            rel_index = iter + 0.5;
                        } else {
                            rel_index = floor(iter + count / 2.0);
                        }

                        planResult_ptr->RSFC[qi][qj].insert(planResult_ptr->RSFC[qi][qj].begin(),
                                                            std::make_pair(sec2normVec(sector_range[sector_curr]),
                                                                           rel_index * param.time_step));
                        planResult_ptr->T.emplace_back(rel_index);

                        sector_next = sector_curr;
                        iter = iter - sector_log.col(iter).maxCoeff() + 1;
                    }
                }
            }

            timer.stop();
            ROS_INFO_STREAM("Corridor: RSFC runtime=" << timer.elapsedSeconds());
        }

        octomap::point3d sec2normVec(int sector) {
            octomap::point3d n;
            n.x() = 0;
            n.y() = 0;
            n.z() = 0;
            int sgn = (sector > 0) - (sector < 0);

            switch (abs(sector)) {
                case 1:
                    n.x() = sgn;
                    break;
                case 2:
                    n.y() = sgn;
                    break;
                case 3:
                    n.z() = sgn / param.downwash;
                    break;
                default:
                    ROS_ERROR("Corridor: Invalid sector number.");
                    break;
            }

            return n;
        }

        bool updateTs() {
            Timer timer;

            planResult_ptr->T.emplace_back(0);
            planResult_ptr->T.emplace_back(makespan);

//        // Delete redundant time delay
//        for(int qi = 0; qi < mission.qn; qi++){
//            for(int qj = qi + 1; qj < mission.qn; qj++){
//                for(int ri=0; ri<RSFC[qi][qj].size(); ri++){
//                    double t = RSFC[qi][qj][ri].second;
//                    if(t-floor(t) > 0.001){
//                        std::vector<double> obsbox_i;
//                        int bi = 0;
//                        while(bi < SFC[qi].size() && SFC[qi][bi].second < floor(t)){
//                            bi++;
//                        }
//                        if(SFC[qi][bi].second == floor(t)){
//                            //bi bi+1
//                            for(int i=0; i<3; i++) {
//                                obsbox_i.emplace_back(std::max(SFC[qi][bi].first[i],
//                                                               SFC[qi][bi+1].first[i]));
//                            }
//                            for(int i=3; i<6; i++) {
//                                obsbox_i.emplace_back(std::min(SFC[qi][bi].first[i],
//                                                               SFC[qi][bi+1].first[i]));
//                            }
//                        }
//                        else{
//                            //bi
//                            obsbox_i = SFC[qi][bi].first;
//                        }
//
//                        std::vector<double> obsbox_j;
//                        int bj = 0;
//                        while(bj < SFC[qj].size() && SFC[qj][bj].second < floor(t)){
//                            bj++;
//                        }
//                        if(SFC[qj][bj].second == floor(t)){
//                            // bj bj+1
//                            for(int j=0; j<3; j++) {
//                                obsbox_j.emplace_back(std::max(SFC[qj][bj].first[j],
//                                                               SFC[qj][bj+1].first[j]));
//                            }
//                            for(int j=3; j<6; j++) {
//                                obsbox_j.emplace_back(std::min(SFC[qj][bj].first[j],
//                                                               SFC[qj][bj+1].first[j]));
//                            }
//                        }
//                        else{
//                            obsbox_j = SFC[qj][bj].first;
//                        }
//
//                        bool flag1 = true;
//                        bool flag2 = true;
//                        int sector1 = RSFC[qi][qj][ri].first;
//                        int sector2 = RSFC[qi][qj][ri+1].first;
//
//                        if(sector1 > 0 && obsbox_j[sector1+3-1] < obsbox_i[sector1-1]+mission.quad_size[qi]+mission.quad_size[qj]){ //z axis coeff ////////////////////
//                            flag1 = false;
//                        }
//                        else if(sector1 < 0 && obsbox_j[abs(sector1)-1] > obsbox_i[abs(sector1)+3-1]-mission.quad_size[qi]-mission.quad_size[qj]){
//                            flag1 = false;
//                        }
//
//                        if(sector2 > 0 && obsbox_j[sector2+3-1] < obsbox_i[sector2-1]+mission.quad_size[qi]+mission.quad_size[qj]){ //z axis coeff ////////////////////
//                            flag2 = false;
//                        }
//                        else if(sector2 < 0 && obsbox_j[abs(sector2)-1] > obsbox_i[abs(sector2)+3-1]-mission.quad_size[qi]-mission.quad_size[qj]){
//                            flag2 = false;
//                        }
//
//                        if(flag1 && flag2){
//                            T.erase(std::find(T.begin(), T.end(), t));
//                            T.emplace_back(floor(t));
//                            RSFC[qi][qj][ri].second = floor(t);
//                        }
//                    }
//                }
//            }
//        }

            // update ts_total
            std::sort(planResult_ptr->T.begin(), planResult_ptr->T.end());
            planResult_ptr->T.erase(std::unique(planResult_ptr->T.begin(), planResult_ptr->T.end()),
                                    planResult_ptr->T.end());

//        // check isolate box and update ts_each
//        for(int qi = 0; qi < mission.qn; qi++){
//            ts_each[qi].emplace_back(0);
//            ts_each[qi].emplace_back(makespan);
//
//            for(int qj = qi + 1; qj < qn; qj++){
//                for(int ti = 1; ti < ts_total.size(); ti++){
//                    int bi = 0;
//                    while(bi < obstacle_boxes[qi].size() && obstacle_boxes[qi][bi].second < ts_total[ti]){
//                        bi++;
//                    }
//                    int bj = 0;
//                    while(bj < obstacle_boxes[qj].size() && obstacle_boxes[qj][bj].second < ts_total[ti]){
//                        bj++;
//                    }
//
//                    if(isBoxInBox(obstacle_boxes[qi][bi].first, obstacle_boxes[qj][bj].first)){
//                        ts_each[qi].emplace_back(ts_total[ti-1]);
//                        ts_each[qj].emplace_back(ts_total[ti]);
//                    }
//                }
//            }
//
//            std::sort(ts_each[qi].begin(), ts_each[qi].end());
//            ts_each[qi].erase(std::unique(ts_each[qi].begin(), ts_each[qi].end()), ts_each[qi].end());
//        }

            // scaling
            for (int i = 0; i < planResult_ptr->T.size(); i++) {
                planResult_ptr->T[i] = planResult_ptr->T[i] * param.time_step;
            }

            timer.stop();
            ROS_INFO_STREAM("Corridor: segment time runtime: " << timer.elapsedSeconds());
            return true;
        }
    };
}