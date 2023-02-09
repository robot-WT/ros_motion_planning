#include "lpa_star.h"

namespace lpa_star_planner
{
LPAStar::LPAStar(int nx, int ny, double resolution) : global_planner::GlobalPlanner(nx, ny, resolution)
{
  curr_global_costmap_ = new unsigned char[ns_];
  last_global_costmap_ = new unsigned char[ns_];
  goal_.x = goal_.y = INF;
  factor_ = 0.35;
  initMap();
}

void LPAStar::initMap()
{
  map_ = new LNodePtr*[nx_];
  for (int i = 0; i < nx_; i++)
  {
    map_[i] = new LNodePtr[ny_];
    for (int j = 0; j < ny_; j++)
    {
      map_[i][j] = new LNode(i, j, INF, INF, this->grid2Index(i, j), -1, INF, INF);
      map_[i][j]->open_it = open_list_.end();  // allocate empty memory
    }
  }
}

void LPAStar::reset()
{
  open_list_.clear();

  for (int i = 0; i < nx_; i++)
    for (int j = 0; j < ny_; j++)
      delete map_[i][j];

  for (int i = 0; i < nx_; i++)
    delete[] map_[i];

  delete[] map_;

  initMap();
}

double LPAStar::getH(LNodePtr s)
{
  return std::hypot(s->x - goal_.x, s->y - goal_.y);
}

double LPAStar::calculateKey(LNodePtr s)
{
  return std::min(s->cost, s->rhs) + this->getH(s);
}

bool LPAStar::isCollision(LNodePtr n1, LNodePtr n2)
{
  return curr_global_costmap_[n1->id] > lethal_cost_ * factor_ || curr_global_costmap_[n2->id] > lethal_cost_ * factor_;
}

void LPAStar::getNeighbours(LNodePtr u, std::vector<LNodePtr>& neighbours)
{
  int x = u->x, y = u->y;
  for (int i = -1; i <= 1; i++)
  {
    for (int j = -1; j <= 1; j++)
    {
      if (i == 0 && j == 0)
        continue;

      int x_n = x + i, y_n = y + j;
      if (x_n < 0 || x_n > nx_ || y_n < 0 || y_n > ny_)
        continue;
      LNodePtr neigbour_ptr = map_[x_n][y_n];

      // if (this->isCollision(u, neigbour_ptr))
      //   continue;

      neighbours.push_back(neigbour_ptr);
    }
  }
}

double LPAStar::getCost(LNodePtr n1, LNodePtr n2)
{
  if (this->isCollision(n1, n2))
    return INF;
  return std::hypot(n1->x - n2->x, n1->y - n2->y);
}

void LPAStar::updateVertex(LNodePtr u)
{
  // u != start
  if (u->x != start_.x || u->y != start_.y)
  {
    std::vector<LNodePtr> neigbours;
    this->getNeighbours(u, neigbours);
    for (LNodePtr s : neigbours)
    {
      if (s->cost + this->getCost(s, u) < u->rhs)
      {
        u->rhs = s->cost + this->getCost(s, u);
        // u->pid = s->id;
      }
    }
  }

  // u in openlist, remove u
  if (u->open_it != open_list_.end())
  {
    open_list_.erase(u->open_it);
    u->open_it = open_list_.end();
  }

  // g(u) != rhs(u)
  if (u->cost != u->rhs)
  {
    u->key = this->calculateKey(u);
    u->open_it = open_list_.insert(std::make_pair(u->key, u));
  }
}

void LPAStar::computeShortestPath()
{
  while (1)
  {
    if (open_list_.empty())
      break;

    LNodePtr u = open_list_.begin()->second;
    open_list_.erase(open_list_.begin());
    u->open_it = open_list_.end();
    expand_.push_back(*u);

    // goal reached
    if (u->key >= this->calculateKey(goal_ptr_) && goal_ptr_->rhs == goal_ptr_->cost)
      break;

    // Locally over-consistent -> Locally consistent
    if (u->cost > u->rhs)
    {
      u->cost = u->rhs;
    }
    // Locally under-consistent -> Locally over-consistent
    else
    {
      u->cost = INF;
      this->updateVertex(u);
    }

    std::vector<LNodePtr> neigbours;
    this->getNeighbours(u, neigbours);
    for (LNodePtr s : neigbours)
      this->updateVertex(s);
  }
}

void LPAStar::extractPath(const Node& start, const Node& goal)
{
  // LNodePtr node_ptr = map_[goal.x][goal.y];
  // while (node_ptr->x != start.x || node_ptr->y != start.y)
  // {
  //   path_.push_back(*node_ptr);
  //   int x, y;
  //   this->index2Grid(node_ptr->pid, x, y);
  //   node_ptr = map_[x][y];
  // }
  // std::reverse(path_.begin(), path_.end());

  LNodePtr node_ptr = map_[goal.x][goal.y];
  while (node_ptr->x != start.x || node_ptr->y != start.y)
  {
    path_.push_back(*node_ptr);
    std::vector<LNodePtr> neigbours;
    this->getNeighbours(node_ptr, neigbours);
    double min_cost = INF;
    LNodePtr next_node_ptr;
    for (LNodePtr node_n_ptr : neigbours)
    {
      if (node_n_ptr->cost < min_cost)
      {
        min_cost = node_n_ptr->cost;
        next_node_ptr = node_n_ptr;
      }
    }
    node_ptr = next_node_ptr;
  }
}

Node LPAStar::getState(const Node& current)
{
  Node state(path_[0].x, path_[0].y);
  int dis_min = std::hypot(state.x - current.x, state.y - current.y);
  int idx_min = 0;
  for (int i = 1; i < path_.size(); i++)
  {
    int dis = std::hypot(path_[i].x - current.x, path_[i].y - current.y);
    if (dis < dis_min)
    {
      dis_min = dis;
      idx_min = i;
    }
  }
  state.x = path_[idx_min].x;
  state.y = path_[idx_min].y;

  return state;
}

std::tuple<bool, std::vector<Node>> LPAStar::plan(const unsigned char* costs, const Node& start, const Node& goal,
                                                  std::vector<Node>& expand)
{
  // update costmap
  memcpy(last_global_costmap_, curr_global_costmap_, ns_);
  memcpy(curr_global_costmap_, costs, ns_);

  expand_.clear();

  if (goal_.x != goal.x || goal_.y != goal.y)
  {
    this->reset();
    goal_ = goal;

    start_ptr_ = map_[start.x][start.y];
    goal_ptr_ = map_[goal.x][goal.y];

    start_ptr_->rhs = 0;
    start_ptr_->key = this->calculateKey(start_ptr_);
    start_ptr_->open_it = open_list_.insert(std::make_pair(start_ptr_->key, start_ptr_));

    computeShortestPath();

    path_.clear();
    this->extractPath(start, goal);

    expand = expand_;

    return { true, path_ };
  }
  else
  {
    Node state = this->getState(start);

    for (int i = -WINDOW_SIZE / 2; i < WINDOW_SIZE / 2; i++)
    {
      for (int j = -WINDOW_SIZE / 2; j < WINDOW_SIZE / 2; j++)
      {
        int x_n = state.x + i, y_n = state.y + j;
        if (x_n < 0 || x_n > nx_ || y_n < 0 || y_n > ny_)
          continue;

        int idx = this->grid2Index(x_n, y_n);
        if (curr_global_costmap_[idx] != last_global_costmap_[idx])
        {
          ROS_WARN("(%d, %d) changed!", x_n, y_n);
          LNodePtr u = map_[x_n][y_n];
          std::vector<LNodePtr> neigbours;
          this->updateVertex(u);
          for (LNodePtr s : neigbours)
          {
            this->updateVertex(s);
          }
        }
      }
    }
    computeShortestPath();

    path_.clear();
    this->extractPath(state, goal);

    return { true, path_ };
  }
}
}  // namespace lpa_star_planner