#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"

using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi()
{
    return M_PI;
}
double deg2rad(double x)
{
    return x * pi() / 180;
}
double rad2deg(double x)
{
    return x * 180 / pi();
}

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s)
{
    auto found_null = s.find("null");
    auto b1 = s.find_first_of("[");
    auto b2 = s.find_first_of("}");
    if (found_null != string::npos)
    {
        return "";
    }
    else if (b1 != string::npos && b2 != string::npos)
    {
        return s.substr(b1, b2 - b1 + 2);
    }
    return "";
}

double distance(double x1, double y1, double x2, double y2)
{
    return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

    double closestLen = 100000; //large number
    int closestWaypoint = 0;

    for(int i = 0; i < maps_x.size(); i++)
    {
        double map_x = maps_x[i];
        double map_y = maps_y[i];
        double dist = distance(x,y,map_x,map_y);
        if(dist < closestLen)
        {
            closestLen = dist;
            closestWaypoint = i;
        }

    }

    return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

    int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

    double map_x = maps_x[closestWaypoint];
    double map_y = maps_y[closestWaypoint];

    double heading = atan2((map_y-y),(map_x-x));

    // angle: car's angle
    double angle = fabs(theta-heading);
    angle = min(2*pi() - angle, angle);

    if(angle > pi()/4)
    {
        closestWaypoint++;
        if (closestWaypoint == maps_x.size())
        {
            closestWaypoint = 0;
        }
    }

    return closestWaypoint;
}


// TODO (Rebecca#4#): Maybe improve the function getFrenet
// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
    int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

    int prev_wp;
    prev_wp = next_wp-1;
    if(next_wp == 0)
    {
        prev_wp  = maps_x.size()-1;
    }

    double n_x  = maps_x[next_wp]-maps_x[prev_wp];
    double n_y = maps_y[next_wp]-maps_y[prev_wp];
    double x_x = x - maps_x[prev_wp];
    double x_y = y - maps_y[prev_wp];

    // find the projection of x onto n
    double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
    double proj_x = proj_norm*n_x;
    double proj_y = proj_norm*n_y;

    double frenet_d = distance(x_x,x_y,proj_x,proj_y);

    //see if d value is positive or negative by comparing it to a center point

    double center_x = 1000-maps_x[prev_wp];
    double center_y = 2000-maps_y[prev_wp];
    double centerToPos = distance(center_x,center_y,x_x,x_y);
    double centerToRef = distance(center_x,center_y,proj_x,proj_y);

    if(centerToPos <= centerToRef)
    {
        frenet_d *= -1;
    }

    // calculate s value
    double frenet_s = 0;
    for(int i = 0; i < prev_wp; i++)
    {
        frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
    }

    frenet_s += distance(0,0,proj_x,proj_y);

    return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
    int prev_wp = -1;

    while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
    {
        prev_wp++;
    }

    int wp2 = (prev_wp+1)%maps_x.size();

    double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
    // the x,y,s along the segment
    double seg_s = (s-maps_s[prev_wp]);

    double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
    double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

    double perp_heading = heading-pi()/2;

    double x = seg_x + d*cos(perp_heading);
    double y = seg_y + d*sin(perp_heading);

    return {x,y};

}

/*
If the car is too slow, the cost approaches 1.
  The lower the velocity, the faster the cost grows.
  If the car is faster than speed limit, the cost reaches max 1.
*/
double CostVelocity( double goalSpeed, double egoSpeed)
{
    double cost= 1- exp((egoSpeed-goalSpeed)/goalSpeed*2.0);  // use power

    cost = pow((egoSpeed - goalSpeed)/goalSpeed,2.0); // use quadratic
    if (egoSpeed>goalSpeed)
        cost=1.0;

    return cost;
}
/*
cost of adjacent car too close to the ego car (both in s and v)
*/
double CostCheckcar(double egocarS, double checkcarS, double egoSpeed, double checkSpeed, double safe_dist, double goalSpeed, double deltaSpeed)
{
    double cost=0.0;
    if (fabs((egocarS-checkcarS)>= safe_dist))
        cost=0.0;
    else if (egocarS < checkcarS && checkcarS < egocarS+safe_dist)
    {
        cost = 1 - (checkcarS- egocarS )/safe_dist;
        cost += (egoSpeed - checkSpeed)/egoSpeed;
        cost += CostVelocity(goalSpeed, egoSpeed-deltaSpeed);
    }
    else if (checkcarS>egocarS-safe_dist && egocarS>checkcarS)
    {
        cost = 1 - (egocarS-checkcarS)/safe_dist;
        cost += (checkSpeed-egoSpeed)/egoSpeed;
        cost += CostVelocity(goalSpeed, egoSpeed+deltaSpeed);
    }
    return cost;
}

int main()
{
    uWS::Hub h;

    // Load up map values for waypoint's x,y,s and d normalized normal vectors
    vector<double> map_waypoints_x;
    vector<double> map_waypoints_y;
    vector<double> map_waypoints_s;
    vector<double> map_waypoints_dx;
    vector<double> map_waypoints_dy;

    // Waypoint map to read from
    string map_file_ = "../data/highway_map.csv";
    // The max s value before wrapping around the track back to 0
    double max_s = 6945.554;

    ifstream in_map_(map_file_.c_str(), ifstream::in);

    string line;
    while (getline(in_map_, line))
    {
        istringstream iss(line);
        double x;
        double y;
        float s;
        float d_x;
        float d_y;
        iss >> x;
        iss >> y;
        iss >> s;
        iss >> d_x;
        iss >> d_y;
        map_waypoints_x.push_back(x);
        map_waypoints_y.push_back(y);
        map_waypoints_s.push_back(s);
        map_waypoints_dx.push_back(d_x);
        map_waypoints_dy.push_back(d_y);
    }

    // the lane# to be followed
    int lane = 1;
    // reference velocity
    double ref_vel = 0.0; // 49.5; //mph


    h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy, &lane, &ref_vel](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                uWS::OpCode opCode)
    {
        // "42" at the start of the message means there's a websocket message event.
        // The 4 signifies a websocket message
        // The 2 signifies a websocket event
        //auto sdata = string(data).substr(0, length);
        //cout << sdata << endl;
        if (length && length > 2 && data[0] == '4' && data[1] == '2')
        {

            auto s = hasData(data);

            if (s != "")
            {
                auto j = json::parse(s);

                string event = j[0].get<string>();

                if (event == "telemetry")
                {
                    // j[1] is the data JSON object

                    // Main car's localization Data
                    double car_x = j[1]["x"];
                    double car_y = j[1]["y"];
                    double car_s = j[1]["s"];
                    double car_d = j[1]["d"];
                    double car_yaw = j[1]["yaw"];
                    double car_speed = j[1]["speed"];

                    // Previous path data given to the Planner
                    auto previous_path_x = j[1]["previous_path_x"];
                    auto previous_path_y = j[1]["previous_path_y"];
                    // Previous path's end s and d values
                    double end_path_s = j[1]["end_path_s"];
                    double end_path_d = j[1]["end_path_d"];

                    // Sensor Fusion Data, a list of all other cars on the same side of the road.
                    auto sensor_fusion = j[1]["sensor_fusion"];

                    json msgJson;

                    vector<double> next_x_vals;
                    vector<double> next_y_vals;


                    // TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
                    double VL = 49.5; // mph, speed limit


                    int prev_size = previous_path_x.size();


                    if (prev_size>0)
                        car_s = end_path_s;

                    bool too_close = false;
                    bool leftlane_tooclose = false;
                    bool rightlane_tooclose = false;


                    double safe_dist = 30;


                    double cost_KL=0.0;
                    double cost_LCL = 0.0;
                    double cost_LCR = 0.0;

                    double frontcar_cost = CostVelocity(VL, ref_vel-0.224);
                    double backcar_cost = CostVelocity(VL, ref_vel+0.224);
                    //find ref_v to use
                    for (int i=0; i< sensor_fusion.size(); i++)
                    {
                        // car is in my lane
                        double d = sensor_fusion[i][6];  //  [ id, x, y, vx, vy, s, d]
                        double vx = sensor_fusion[i][3];
                        double vy = sensor_fusion[i][4];
                        double check_speed = distance(0,0,vx,vy);
                        double check_car_s = sensor_fusion[i][5];
                        // use previous # of points to project s beyond the  current location of [id] car
                        check_car_s += (double) prev_size * 0.02 * check_speed;
                        int check_lane = (int) (d/4);
                        double changelane_cost = CostCheckcar(car_s, check_car_s, car_speed, check_speed, safe_dist, VL, 0.224);
                        // check if it's in the ego car's lane
                        if (check_lane == lane)
                        {
                            if ((check_car_s > car_s && (check_car_s - car_s)<safe_dist ))
                            {
                                //ref_vel = 29.5;  // mph
                                too_close = true;

                                if (cost_KL < frontcar_cost)
                                {
                                    cost_KL = frontcar_cost;
                                }

                            }
                        }
                        else if (check_lane == lane-1)
                        {
                            if ((check_car_s > car_s && (check_car_s - car_s)<safe_dist ))
                                leftlane_tooclose = true;
                            if (cost_LCL < changelane_cost)
                            {
                                cost_LCL = changelane_cost;
                            }

                        }
                        else if (check_lane == lane+1)
                        {
                            if ((check_car_s > car_s && (check_car_s - car_s)<safe_dist ))
                                rightlane_tooclose = true;
                            if (cost_LCR < changelane_cost)
                                cost_LCR = changelane_cost;
                        }


                    }

                    if (too_close)
                    {
                        //ref_vel -= 0.224;

                        if (lane==0)
                        {
                            if (cost_LCR<cost_KL)
                            {
                                lane +=1;
                                if (rightlane_tooclose)
                                    ref_vel-= 0.224;
                            }
                            else
                                ref_vel-= 0.224;
                        }
                        else if (lane==1)
                        {
                            if (cost_LCL<cost_KL)
                            {
                                lane -=1;
                                if (leftlane_tooclose)
                                    ref_vel-=0.224;
                            }
                            else if (cost_LCR<cost_KL)
                            {
                                lane +=1;
                                if (rightlane_tooclose)
                                    ref_vel-= 0.224;
                            }
                            else
                                ref_vel -= 0.224;

                        }
                        else
                        {
                            if (cost_LCL<cost_KL)
                            {
                                lane -=1;
                                if (leftlane_tooclose)
                                    ref_vel-=0.224;
                            }
                            else
                                ref_vel -= 0.224;
                        }

                    }
                    else if (ref_vel<VL)
                    {
                        ref_vel += 0.224;   // 0.1 m/s
                    }

                    // create a list of widely spaced (x,y) waypoints, and interpolate them with a spline and fill it with more points to control speed.
                    vector<double> ptsx;
                    vector<double> ptsy;

                    // reference x, y, yaw states: either the current state of the car, or the previous path end points
                    double ref_x, ref_y, ref_yaw;
                    double prev_car_x, prev_car_y;

                    if (prev_size < 2)  // if previous path has only 1 point, use current car as reference
                    {
                        // use a prev point calculated from the current point and current yaw, and it is tangent to current path
                        ref_x = car_x;
                        ref_y = car_y;
                        ref_yaw = deg2rad(car_yaw);
                        prev_car_x = car_x - cos(car_yaw);
                        prev_car_y = car_y - sin(car_yaw);

                    }
                    else
                    {
                        ref_x = previous_path_x[prev_size-1];
                        ref_y = previous_path_y[prev_size-1];

                        prev_car_x = previous_path_x[prev_size-2];
                        prev_car_y = previous_path_y[prev_size-2];
                        ref_yaw = atan2(ref_y - prev_car_y, ref_x-prev_car_x);
                    }
                    ptsx.push_back(prev_car_x);
                    ptsx.push_back(ref_x);
                    ptsy.push_back(prev_car_y);
                    ptsy.push_back(ref_y);

                    // In frenet world, add evenly 30m spaced points in addition
                    vector<double> next_wp0 = getXY(car_s+30, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
                    vector<double> next_wp1 = getXY(car_s+60, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
                    vector<double> next_wp2 = getXY(car_s+90, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
                    ptsx.push_back(next_wp0[0]);
                    ptsx.push_back(next_wp1[0]);
                    ptsx.push_back(next_wp2[0]);
                    ptsy.push_back(next_wp0[1]);
                    ptsy.push_back(next_wp1[1]);
                    ptsy.push_back(next_wp2[1]);

                    // transform the map coordinates to the car's coordinates
                    for (int i=0; i<ptsx.size(); i++)
                    {

                        double shift_x = ptsx[i]-ref_x;
                        double shift_y = ptsy[i]-ref_y;
                        double shift_angle = 0-ref_yaw;
                        ptsx[i] = (shift_x *cos(shift_angle) - shift_y * sin(shift_angle));
                        ptsy[i] = (shift_x *sin(shift_angle) + shift_y * cos(shift_angle));
                    }

                    tk::spline s;
                    s.set_points(ptsx,ptsy);

                    // firstly, set all previous path points to the vector list
                    for (int i=0; i<previous_path_x.size(); i++)
                    {

                        next_x_vals.push_back(previous_path_x[i]);
                        next_y_vals.push_back(previous_path_y[i]);
                    }

                    // calculate how to break up spline so as to travel at desired ref velocity (limit)
                    double target_x = 30.0;
                    double target_y = s(target_x);
                    double target_distance = distance(0,0, target_x, target_y);// sqrt(target_x*target_x + target_y*target_y);

                    double x_add_on = 0.0;
                    double N = target_distance/(0.02 * ref_vel/2.24);   // 0.02s * ref_vel/2.24 m/s
                    //start from previous points, and fill in the rest of the path planner until 50 points
                    for (int i=1; i<=50-previous_path_x.size(); i++)
                    {

                        double x_point = x_add_on + target_x/N;
                        double y_point = s(x_point);

                        x_add_on = x_point;  // x_add_on always on local coord. of the x-axis in the triangle plot
                        double x_ref = x_point;
                        double y_ref = y_point;

                        x_point = x_ref * cos(ref_yaw) - y_ref * sin(ref_yaw);
                        y_point = x_ref * sin(ref_yaw) + y_ref * cos(ref_yaw);

                        x_point += ref_x;
                        y_point += ref_y;

                        next_x_vals.push_back(x_point);
                        next_y_vals.push_back(y_point);

                    }

                    // END

                    msgJson["next_x"] = next_x_vals;
                    msgJson["next_y"] = next_y_vals;

                    auto msg = "42[\"control\","+ msgJson.dump()+"]";

                    //this_thread::sleep_for(chrono::milliseconds(1000));
                    ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);

                }
            }
            else
            {
                // Manual driving
                std::string msg = "42[\"manual\",{}]";
                ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
            }
        }
    });

    // We don't need this since we're not using HTTP but if it's removed the
    // program
    // doesn't compile :-(
    h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                       size_t, size_t)
    {
        const std::string s = "<h1>Hello world!</h1>";
        if (req.getUrl().valueLength == 1)
        {
            res->end(s.data(), s.length());
        }
        else
        {
            // i guess this should be done more gracefully?
            res->end(nullptr, 0);
        }
    });

    h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req)
    {
        std::cout << "Connected!!!" << std::endl;
    });

    h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                           char *message, size_t length)
    {
        ws.close();
        std::cout << "Disconnected" << std::endl;
    });

    int port = 4567;
    if (h.listen(port))
    {
        std::cout << "Listening to port " << port << std::endl;
    }
    else
    {
        std::cerr << "Failed to listen to port" << std::endl;
        return -1;
    }
    h.run();
}
