/*!
  \file        tiled2sdl.h
  \author      Arnaud Ramey <arnaud.a.ramey@gmail.com>
                -- Robotics Lab, University Carlos III of Madrid
  \date        2017/1/6
________________________________________________________________________________

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
________________________________________________________________________________

\todo Description of the file
 */

#ifndef TILED2SDL_H
#define TILED2SDL_H

#include <rostiled/utils.h>
#include <boost/shared_ptr.hpp>

#define TWOPI    6.28318530717958647693
#define RAD2DEG  57.2957795130823208768
#define DEG2RAD  0.01745329251994329577

////////////////////////////////////////////////////////////////////////////////

// http://stackoverflow.com/questions/37907758/how-to-avoid-the-need-to-specify-deleter-for-stdshared-ptr-every-time-its-con
typedef boost::shared_ptr<SDL_Texture> SDL_TexturePtr;

class Movable {
public:
  Movable() : _maxspeed(1), _xortho(0), _yortho(0), _angle(0), _xspeed(0), _yspeed(0),
    _ntextures(0) {}

  bool create(SDL_Renderer* renderer,
              const std::string & xml_file,
              double maxspeed = 1,
              double xortho = 0, double yortho = 0, double angle = 0) {
    if (!renderer) {
      printf("Movable::create(): empty renderer!\n");
      return false;
    }
    _maxspeed = maxspeed;
    _xortho = xortho;
    _yortho = yortho;
    _angle = angle;
    /*parse the file and get the DOM */
    std::string folder_name = extract_folder_from_full_path(xml_file);
    xmlDoc *doc = xmlReadFile(xml_file.c_str(), NULL, 0);
    if (doc == NULL){
      printf("error: could not parse file %s\n", xml_file.c_str());
      return false;
    }
    xmlNode* root_element = xmlDocGetRootElement(doc);
    double scale = atof((char*) xmlGetProp(root_element, (xmlChar*) "scale"));
    printf("Scale:%g\n", scale);
    xmlNode* cur = root_element->xmlChildrenNode;
    while (cur != NULL) {
      if (!xmlStrEqual(cur->name, (xmlChar*) "view")) {
        cur = cur->next;
        continue;
      }
      std::string filename = folder_name + (char*) xmlGetProp(cur, (xmlChar*) "file");
      double angle = atof((char*) xmlGetProp(cur, (xmlChar*) "angle")) * DEG2RAD;
      SDL_Point center;
      center.x = scale * atoi((char*) xmlGetProp(cur, (xmlChar*) "centerx"));
      center.y = scale * atoi((char*) xmlGetProp(cur, (xmlChar*) "centery"));
      SDL_Surface* surf = IMG_Load(filename.c_str());
      if (!surf) {
        printf("Could not load surface '%s', %g\n", filename.c_str(), angle);
        return false;
      }
      SDL_Surface* surf_scaled = ScaleSurface(surf, scale*surf->w, scale*surf->h);
      //printf("surf_scaled:(%i, %i)\n", surf_scaled->w, surf_scaled->h);

      SDL_TexturePtr tex (SDL_CreateTextureFromSurface( renderer, surf_scaled ),
                          SDL_DestroyTexture);
      if (!tex) {
        printf("Could not load texture '%s', %g\n", filename.c_str(), angle);
        return false;
      }
      SDL_Rect size;
      size.x = size.y = 0;
      if (SDL_QueryTexture(tex.get(), NULL, NULL, &(size.w), &(size.h)) != 0) {
        printf("SDL_QueryTexture() returned an error '%s'\n", SDL_GetError());
        return false;
      }
      printf("Adding '%s', angle:%g°, center:(%i, %i), size:(%i, %i)\n",
             filename.c_str(), angle * RAD2DEG, center.x, center.y, size.w, size.h);
      if (!size.w || !size.h) {
        printf("the texture obtained by SDL_CreateTextureFromSurface() is empty!\n");
        return false;
      }
      _textures.push_back(tex);
      _sizes.push_back(size);
      _angles.push_back(angle);
      _cosangles.push_back(cos(angle));
      _sinangles.push_back(sin(angle));
      _centers.push_back(center);
      cur = cur->next;
      // clean
      SDL_FreeSurface( surf );
      SDL_FreeSurface( surf_scaled );
    } // end while (cur)
    _ntextures = _textures.size();
    xmlFreeDoc(doc);
    return true;
  } // end ctor

  void advance(const double & dist) {
    _xortho += cos(-_angle) * dist;
    _yortho += sin(-_angle) * dist;
  }

  bool update(tmx_map* map, tmx_layer* obstacle_layer,
              const std::vector<bool> & diamond_hittest) {
    Timer::Time time = _update_timer.getTimeSeconds();
    _update_timer.reset();
    double currspeed = hypot(_xspeed, _yspeed);
    // rotate towards speed direction
    if (currspeed > 1E-2)
      _angle = atan2(_yspeed, _xspeed);
    // normalize speed if going too fast
    if (currspeed > _maxspeed) {
      //_xspeed = cos(_angle) * _maxspeed;
      //_yspeed = sin(_angle) * _maxspeed;
    }
    double newx = _xortho + time * _xspeed;
    double newy = _yortho + time * _yspeed;
    // check if there is a collision - note the conversion to isometric
    if (point_in_layer_staggered(map, obstacle_layer, newx, newy/2, diamond_hittest)) {
      //printf("Collision detected, not moving.\n");
    }
    else {
      _xortho = newx;
      _yortho = newy;
    }
    return true;
  }

  bool render(SDL_Renderer* renderer) {
    // find best texture
    if (!_ntextures ||  _ntextures != _angles.size()) {
      printf("Car::render(): there is no texture, or their size doesn't match with angles!\n");
      return false;
    }
    unsigned int best_idx = 0;
    double best_dist = 1E6, cosa = cos(_angle), sina = -sin(_angle);
    for (unsigned int i = 0; i < _ntextures; ++i) {
      double dist = fabs(_cosangles[i] - cosa) + fabs(_sinangles[i] - sina);
      if (best_dist > dist) {
        //printf("New best diff:%i, %g\n", i, dist);
        best_idx = i;
        best_dist = dist;
      }
    } // end for i
    // render it
    SDL_TexturePtr tex = _textures[best_idx];
    SDL_Rect srcrect = _sizes[best_idx], dstrect = _sizes[best_idx];
    dstrect.x = _xortho - _centers[best_idx].x;
    dstrect.y = _yortho / 2 - _centers[best_idx].y; // conversion to isometric
    //  printf("Car::render(%g, %g, angle:%g°)-->tex #%i/%i, size:(%i, %i), "
    //         "srcrect:%s, dstrect:%s\n",
    //         _xortho, _yortho, _angle * RAD2DEG, best_idx, _ntextures,
    //         _sizes[best_idx].w, _sizes[best_idx].h,
    //         rect2str(srcrect).c_str(), rect2str(dstrect).c_str());
    if (SDL_RenderCopy(renderer, tex.get(), &srcrect, &dstrect) < 0) {
      printf("movable:SDL_RenderCopy returned an error '%s'!\n", SDL_GetError());
      return false;
    }
    return true;
  }

  Timer _update_timer;
  double _maxspeed;
  double _xortho, _yortho, _angle, _xspeed, _yspeed;
  std::vector< SDL_TexturePtr > _textures;
  unsigned int _ntextures;
  std::vector<double> _angles, _cosangles, _sinangles;
  std::vector<SDL_Point> _centers;
  std::vector<SDL_Rect> _sizes;
}; // end class Car

////////////////////////////////////////////////////////////////////////////////

SDL_Renderer* renderer_ptr = NULL;
static void* sdl_img_loader(const char *path) {
  return IMG_LoadTexture(renderer_ptr, path);
}

class MapRenderer {
public:
  MapRenderer() : _renderer(NULL), _map(NULL), _nmovables(0) {}

  ~MapRenderer() { clean(true); }

  bool init(const std::string & mapname,
            int winw = 800,
            int winh = 600) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
      printf("Error while SDL_Init():'%s'\n", SDL_GetError());
      return clean(false);
    }
    if (!(_win = SDL_CreateWindow("SDL Game", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                  winw, winh, SDL_WINDOW_SHOWN))) {
      printf("Error while SDL_CreateWindow():'%s'\n", SDL_GetError());
      return clean(false);
    }
    if (!(_renderer = SDL_CreateRenderer(_win, -1, SDL_RENDERER_ACCELERATED  |  SDL_RENDERER_PRESENTVSYNC))) {
      printf("Error while SDL_CreateRenderer():'%s'\n", SDL_GetError());
      return clean(false);
    }
    renderer_ptr = _renderer;

    // load map
    tmx_img_load_func = (void* (*)(const char*)) sdl_img_loader;
    tmx_img_free_func = (void  (*)(void*))      SDL_DestroyTexture;
    if (!(_map = tmx_load(mapname.c_str()))) {
      printf("Error while tmx_load():'%s'\n", tmx_strerr());
      return clean(false);
    }
    // get obstacles layer
    std::string layer_name = "obstacles";
    _obstacles_layer = get_layer(_map, layer_name);
    if (!_obstacles_layer) {
      printf("Could not get layer '%s' in map\n", layer_name.c_str());
      return clean(false);
    }
    // render map
    if (!(map_bmp = render_map(_renderer, _map))) {
      printf("Error while render_map():'%s'\n", SDL_GetError());
      return clean(false);
    }
    // init the ROI of the map
    SDL_QueryTexture(map_bmp, NULL, NULL, &(map_roi.w), &(map_roi.h));
    map_roi.x = 0;  map_roi.y = 0;
    // create diamond hittest
    build_diamond_hittest(_diamond_hittest, _map->tile_width, _map->tile_height);
    return true;
  } // end init()

  //////////////////////////////////////////////////////////////////////////////

  int init_joysticks() {
    //SDL_EventState(SDL_MOUSEMOTION, SDL_DISABLE);
    //Check for joysticks
    printf("Found %i joysticks\n", SDL_NumJoysticks());
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
      SDL_Joystick * curr = SDL_JoystickOpen( i );
      if(curr == NULL) {
        printf( "Warning: Unable to open game controller! SDL Error: %s\n", SDL_GetError() );
        continue;
      }
      printf( "Joystick %i connected\n", i);
      _game_controllers.push_back(curr);
    }
    return _game_controllers.size();
  }

  //////////////////////////////////////////////////////////////////////////////

  bool create_movable(const std::string & xml_file,
                      double maxspeed = 1,
                      double xortho = 0, double yortho = 0, double angle = 0) {
    _nmovables++;
    _movables.push_back(Movable());
    return _movables[_nmovables-1].create(_renderer, xml_file, maxspeed, xortho, yortho, angle);
    //_nmovables = 2;
    //_movables.resize(_nmovables);
    //_movables[0].create(_renderer, xml_file, maxspeed, xortho, yortho, angle);
    //_movables[1].create(_renderer, xml_file, maxspeed, xortho, yortho, angle);
    //return true;
  }

  //////////////////////////////////////////////////////////////////////////////

  bool update() {
    SDL_Event event;
    while (SDL_PollEvent(&event)){
      if (event.type == SDL_QUIT) {
        printf("Got SDL_QUIT\n");
        return false;
      }

      else if ( event.type == SDL_MOUSEMOTION ) {
        bool coll = point_in_layer_staggered(_map, _obstacles_layer,
                                             event.motion.x, event.motion.y,
                                             _diamond_hittest);
        printf("Mouse moved to (%d,%d), coll:%i\n",
               event.motion.x, event.motion.y, coll);
      }

      else if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.scancode == SDL_SCANCODE_Q) {
          printf("Got SDL_SCANCODE_Q\n");
          return false;
        }

        switch (event.key.keysym.scancode) {
          //        case SDL_SCANCODE_LEFT:  map_rect.x += 10; break;
          //        case SDL_SCANCODE_RIGHT: map_rect.x -= 10; break;
          //        case SDL_SCANCODE_UP:    map_rect.y += 10; break;
          //        case SDL_SCANCODE_DOWN:  map_rect.y -= 10; break;
          case SDL_SCANCODE_LEFT:  _movables.front()._angle -= .1; break;
          case SDL_SCANCODE_RIGHT: _movables.front()._angle += 1; break;
          case SDL_SCANCODE_UP:    _movables.front().advance(10); break;
          case SDL_SCANCODE_DOWN:  _movables.front().advance(-5); break;
          default: break;
        } // end switch
      }

      else if( event.type == SDL_JOYAXISMOTION ) {
        //Motion on controller 0
        if( event.jaxis.which <= (int) _nmovables ) {
          Movable* car = &(_movables[event.jaxis.which]);
          if( event.jaxis.axis == 0 ) // X axis motion
            car->_xspeed = event.jaxis.value / 100;
          else if( event.jaxis.axis == 1)
            car->_yspeed = event.jaxis.value / 100;
        }
      } // end SDL_JOYAXISMOTION
    } // end while (SDL_PollEvent)

    // update cars
    for (unsigned int i = 0; i < _nmovables; ++i)
      _movables[i].update(_map, _obstacles_layer, _diamond_hittest);
    return true;
  }

  //////////////////////////////////////////////////////////////////////////////

  bool render() {
    SDL_RenderClear(_renderer);
    // render map
    if (SDL_RenderCopy(_renderer, map_bmp, NULL, &map_roi) != 0) {
      printf("SDL_RenderCopy returned an error!\n");
      return false;
    }
    // render obstacles
    if (!render_cars_obstacles()) {
      printf("render_cars_obstacles() returned an error!\n");
      return false;
    }
    SDL_RenderPresent(_renderer);
    return true;
  }

private:
  bool render_cars_obstacles() {
    _movable_rows.resize(_nmovables);
    for (unsigned int i = 0; i < _nmovables; ++i) {
      int row = _movables[i]._yortho / (_map->tile_height);
      if (row < 0) row = 0;
      else if (row >= (int) _map->height) row = _map->height;
      _movable_rows[i] = row;
      // printf("car row:%i\n", row);
    }

    // render from bottom to top
    for (unsigned int row = 0; row < _map->height; row++) {
      // render cars of this row
      for (unsigned int i = 0; i < _nmovables; ++i) {
        if (_movable_rows[i] == row && !_movables[i].render(_renderer))
          return false;
      } // end for i
      // render tiles of this row
      for (unsigned int col=0; col < _map->width; col++) {
        unsigned int gid = gid_clear_flags(_obstacles_layer->content.gids[(row * _map->width) + col]);
        render_tile(_renderer, _map, _map->tiles[gid], col, row);
      } // end loop col
    } // end loop row
    return true;
  }

  bool clean(bool retval = true) {
    for (unsigned int i = 0; i < _game_controllers.size(); ++i)
      SDL_JoystickClose( _game_controllers[i] );
    xmlCleanupParser();
    xmlMemoryDump();
    tmx_map_free(_map);
    SDL_DestroyTexture(map_bmp);
    SDL_DestroyRenderer(_renderer);
    SDL_DestroyWindow(_win);
    IMG_Quit();
    SDL_Quit();
    return retval;
  }

  //////////////////////////////////////////////////////////////////////////////
protected:
  SDL_Window *_win;
  std::vector<SDL_Joystick*> _game_controllers;
  SDL_Renderer *_renderer;
  std::vector<bool> _diamond_hittest;
  // map stuff
  tmx_map *_map;
  SDL_Texture  * map_bmp;
  SDL_Rect map_roi;
  // obstacles
  tmx_layer* _obstacles_layer;
  unsigned int _nmovables;
  std::vector<Movable> _movables;
  std::vector<unsigned int> _movable_rows;
}; // end class Game


#endif // TILED2SDL_H
