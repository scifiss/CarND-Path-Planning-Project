**Path Planning Project**



The goals / steps of this project are the following:

* to keep inside its lane, avoid hitting other cars
* to pass slower moving traffic all by using localization, sensor fusion, and map data.

## [Rubrics Points](https://review.udacity.com/#!/rubrics/1020/view) 

### 1. Statement of the problem
We are supposed to feed the simulator with a set of points per cycle (50 points for every 0.02 s) so that a car gets timely control of velocity on a certain lane. The ego car runs along one of the 3 lanes on the right hand side of the road, and tries to maintain a speed close to the speed limit 50 mph. If there is another car in front of its current lane, it tries to safely move to another lane which can runs faster. It also remains comfortable with acceleration and jerk not too high.

* Input
A highway map provides coordinates of the points at the double yellow line in middle of the highway. The total length is 6946 m. The car starts from the red point and runs anti clock wise.
![picture alt](report/overview1.png)
![picture alt](report/overview2.png)

* Output
A set of points coordinates in the next coming cycle (0.02 s) in xy coordinate system.

## 1. The code compiles correctly.
The code compiles successfully and generate the object file.

## 2. Valid trajectories
### The car is able to drive at least 4.32 miles without incident..

The car runs for 6.71 miles without incident.
![picture alt](report/runwellfor6miles.png)

### The car drives according to the speed limit
Please refer to the picture above. The car's speed averages at 46 mph.

### Max Acceleration and Jerk are not Exceeded
The acceleration is always below 10m/s2, and the jerk's always below 10m/s^3;

### Car does not have collisions
The car never collides with other cars.

### The car stays in its lane, except for the time between changing lanes
The car stays in its lane, 

### The car is able to change lanes
The car will turn to a nearby car when a car in front of it is too slow, or a car behind it is too fast. It only change lanes when it wants to maintein a higher yet below limit speed, and keeps safe.

## 3. Model documentation


