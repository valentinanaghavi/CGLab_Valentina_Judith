#include "application_solar.hpp"
#include "window_handler.hpp"

#include "utils.hpp"
#include "shader_loader.hpp"
#include "model_loader.hpp"


#include <glbinding/gl/gl.h>
// use gl definitions from glbinding 
using namespace gl;

//dont load gl bindings from glfw
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <vector>

ApplicationSolar::ApplicationSolar(std::string const& resource_path)
 :Application{resource_path}
 ,planet_object{}
 ,star_object{}
 ,m_view_transform{glm::translate(glm::fmat4{}, glm::fvec3{0.0f, 0.0f, 4.0f})}
 ,m_view_projection{utils::calculate_projection_matrix(initial_aspect_ratio)}
{
  //construct the the scene graph with its geometry
  initializeStars();
  initializeSceneGraph();
  initializeGeometry();
  initializeShaderPrograms();
}

ApplicationSolar::~ApplicationSolar() {
  glDeleteBuffers(1, &planet_object.vertex_BO);
  glDeleteBuffers(1, &planet_object.element_BO);
  glDeleteVertexArrays(1, &planet_object.vertex_AO);

  glDeleteBuffers(1, &star_object.vertex_BO);
  glDeleteBuffers(1, &star_object.element_BO);
  glDeleteVertexArrays(1, &star_object.vertex_AO);
}

void ApplicationSolar::render() const {
  
  renderStars();
  
  // iterate through all nodes that are children in the root list 
  for (auto const& child : scene_graph.getRoot() -> getChildrenList()) {
    // get attributes about the specific planet behaviour
    float speed = child -> getSpeed();
    float distance = child -> getDistance();
    float scale_size = child -> getSize();
    glm::fvec3 planetcolor = child -> getPlanetColor();
    glUseProgram(m_shaders.at("planet").handle);
    int location = glGetUniformLocation(m_shaders.at("planet").handle, "planetcolor");
    glUniform3f(location, planetcolor[0], planetcolor[1], planetcolor[2]);

    // cumulate transformation matrix
    //get local matrix of each planet to set the new values
    glm::fmat4 model_matrix = glm::fmat4{1.0f};
    model_matrix = child -> getLocalTransform();
    model_matrix = glm::scale(glm::fmat4{}, glm::fvec3{scale_size, scale_size, scale_size}); //scale size
    model_matrix = glm::rotate(model_matrix, float(glfwGetTime()) * speed, glm::fvec3{0.0f, 1.0f, 0.0f});//speed
    model_matrix = glm::translate(model_matrix, glm::fvec3{0.0f, 0.0f, - distance}); //distance
    //set the new transformation matrix for the current planet
    child -> setLocalTransform(model_matrix);

    renderPlanets(model_matrix);

    //the earth has a child called "moon" which surrounds the earth
    if(child -> getName() == "earth"){
      //cumulate the child transform matrix as a parent of moon to get its own origin for childs 
      glm::fmat4 model_matrix = child -> getLocalTransform();
      float speed = child -> getSpeed();
      float distance = child -> getDistance();
      float scale_size = child -> getSize();
      //calculate tranformation matrix of the earth (parent) to get the curent position
      model_matrix = glm::scale(glm::fmat4{}, glm::fvec3{scale_size, scale_size, scale_size}); //scale size
      model_matrix = glm::rotate(model_matrix, float(glfwGetTime()) * speed, glm::fvec3{0.0f, 1.0f, 0.0f});//speed
      model_matrix = glm::translate(model_matrix, glm::fvec3{0.0f, 0.0f, - distance}); //distance

      // iterate through the children nodes of earth
      for (auto const& earth_children : child -> getChildrenList()){

        float speed_child = earth_children -> getSpeed();
        float distance_child = earth_children -> getDistance();
        float scale_size = child -> getSize();
        //cumulate transformation values of the current child with tranformed origin (earth)
        model_matrix = glm::scale(model_matrix * earth_children -> getLocalTransform(), glm::fvec3{scale_size, scale_size, scale_size}); //scale size
        model_matrix = glm::rotate(model_matrix * earth_children -> getLocalTransform(), 
                                  float(glfwGetTime()) * speed_child, glm::fvec3{0.0f, 1.0f, 0.0f});//speed
        model_matrix = glm::translate(model_matrix , glm::fvec3{0.0f, 0.0f, - distance_child}); //distance
         
        renderPlanets(model_matrix);
        
      }// end inner loop
    } //end if

  } //end loop

    
    //renderStars();

} //end function

void ApplicationSolar::renderPlanets(glm::fmat4 model_matrix) const{
     glUseProgram(m_shaders.at("planet").handle);
    // extra matrix for normal transformation to keep them orthogonal to surface
    glm::fmat4 normal_matrix = glm::inverseTranspose(glm::inverse(m_view_transform) * model_matrix); 
    glUniformMatrix4fv(m_shaders.at("planet").u_locs.at("NormalMatrix"), 
                         1, GL_FALSE, glm::value_ptr(normal_matrix)); 

    glUniformMatrix4fv(m_shaders.at("planet").u_locs.at("ModelMatrix"),
                         1, GL_FALSE, glm::value_ptr(model_matrix));

    // bind the VAO to draw
    glBindVertexArray(planet_object.vertex_AO);

    // draw bound vertex array using bound shader
    glDrawElements(planet_object.draw_mode, planet_object.num_elements, model::INDEX.type, NULL);   
}

void ApplicationSolar::renderStars() const{
     glUseProgram(m_shaders.at("star").handle);
    // extra matrix for normal transformation to keep them orthogonal to surface
    //glm::fmat4 normal_matrix = glm::inverseTranspose(glm::inverse(m_view_transform) * model_matrix); 
    glUniformMatrix4fv(m_shaders.at("star").u_locs.at("ModelViewMatrix"), 
                         1, GL_FALSE, glm::value_ptr(m_view_transform)); 

    glUniformMatrix4fv(m_shaders.at("star").u_locs.at("ProjectionMatrix"),
                         1, GL_FALSE, glm::value_ptr(m_view_projection));

    // bind the VAO to draw
    glBindVertexArray(star_object.vertex_AO);

    // draw bound vertex array using bound shader
    //glDrawElements(star_object.draw_mode, star_object.num_elements, GL_FLOAT, NULL);
    glDrawArrays(star_object.draw_mode, GLint(0), star_object.num_elements);   

   
}

void ApplicationSolar::uploadView() {
  // vertices are transformed in camera space, so camera transform must be inverted
  glm::fmat4 view_matrix = glm::inverse(m_view_transform);
  // upload matrix to gpu
  glUseProgram(m_shaders.at("planet").handle);
  glUniformMatrix4fv(m_shaders.at("planet").u_locs.at("ViewMatrix"),
                     1, GL_FALSE, glm::value_ptr(view_matrix));

  glUseProgram(m_shaders.at("star").handle);
  glUniformMatrix4fv(m_shaders.at("star").u_locs.at("ModelViewMatrix"),
                     1, GL_FALSE, glm::value_ptr(m_view_transform));

}

void ApplicationSolar::uploadProjection() {
  // upload matrix to gpu
  glUseProgram(m_shaders.at("planet").handle);
  glUniformMatrix4fv(m_shaders.at("planet").u_locs.at("ProjectionMatrix"),
                     1, GL_FALSE, glm::value_ptr(m_view_projection));

  glUseProgram(m_shaders.at("star").handle);
  glUniformMatrix4fv(m_shaders.at("star").u_locs.at("ProjectionMatrix"),
                     1, GL_FALSE, glm::value_ptr(m_view_projection));
}

// update uniform locations
void ApplicationSolar::uploadUniforms() { 
  // bind shader to which to upload unforms
  //glUseProgram(m_shaders.at("planet").handle);
  // upload uniform values to new locations
  uploadView();
  uploadProjection();
}

///////////////////////////// intialisation functions /////////////////////////
// load shader sources
void ApplicationSolar::initializeShaderPrograms() {
  // store shader program objects in container
  m_shaders.emplace("planet", shader_program{{{GL_VERTEX_SHADER,m_resource_path + "shaders/simple.vert"},
                                           {GL_FRAGMENT_SHADER, m_resource_path + "shaders/simple.frag"}}});
  // request uniform locations for shader program
  m_shaders.at("planet").u_locs["NormalMatrix"] = -1;
  m_shaders.at("planet").u_locs["ModelMatrix"] = -1;
  m_shaders.at("planet").u_locs["ViewMatrix"] = -1;
  m_shaders.at("planet").u_locs["ProjectionMatrix"] = -1;

  m_shaders.emplace("star", shader_program{{{GL_VERTEX_SHADER,m_resource_path + "shaders/vao.vert"},
                                           {GL_FRAGMENT_SHADER, m_resource_path + "shaders/vao.frag"}}});
  // request uniform locations for shader program
   m_shaders.at("star").u_locs["ModelViewMatrix"] = -1;
   m_shaders.at("star").u_locs["ProjectionMatrix"] = -1;
}

void ApplicationSolar::initializeSceneGraph() {
  //load model information for planets of the solar system
  model planet_model = model_loader::obj(m_resource_path + "models/sphere.obj", model::NORMAL);

  //initialize the root for the scene graph
  auto root = std::make_shared<Node>();


  //construct each planet in each holder and add planets to the scene graph 
  GeometryNode mercury (planet_model);
  auto mercury_holder = std::make_shared<Node>(mercury);
  mercury_holder -> setName("mercury");
  mercury_holder -> setParent(root);  
  mercury_holder -> setDistance(10.0f); //distance to origin
  mercury_holder -> setSpeed(0.6f); //rotation speed
  mercury_holder -> setSize(0.3f);  //scale size
  mercury_holder -> setParent(mercury_holder);
  root -> addChildren(mercury_holder);

  GeometryNode venus (planet_model);
  auto venus_holder = std::make_shared<Node>(venus);
  venus_holder -> setName("venus");
  venus_holder -> setParent(root);
  venus_holder -> setDistance(10.0f); //distance to origin
  venus_holder -> setSpeed(0.5f); //rotation speed
  venus_holder -> setSize(0.5f);  //scale size
  venus_holder -> setParent(venus_holder);
  root -> addChildren(venus_holder);

  GeometryNode mars (planet_model);
  auto mars_holder = std::make_shared<Node>(mars);
  mars_holder -> setName("mars");
  mars_holder -> setParent(root);
  mars_holder -> setDistance(30.0f); //distance to origin
  mars_holder -> setSpeed(0.4f); //rotation speed
  mars_holder -> setSize(0.3f); //scale size
  mars_holder -> setParent(mars_holder);
  root -> addChildren(mars_holder);

  GeometryNode jupiter (planet_model);
  auto jupiter_holder = std::make_shared<Node>(jupiter);
  jupiter_holder -> setName("jupiter");
  jupiter_holder -> setParent(root);
  jupiter_holder -> setDistance(8.0f); //distance to origin
  jupiter_holder -> setSpeed(0.25f); //rotation speed
  jupiter_holder -> setSize(1.8f);  //scale size
  jupiter_holder -> setParent(jupiter_holder);
  root -> addChildren(jupiter_holder);

  GeometryNode saturn (planet_model);
  auto saturn_holder = std::make_shared<Node>(saturn);
  saturn_holder -> setName("saturn");
  saturn_holder -> setParent(root);
  saturn_holder -> setDistance(10.0f); //distance to origin
  saturn_holder -> setSpeed(0.2f); //rotation speed
  saturn_holder -> setSize(1.5f); //scale size
  saturn_holder -> setParent(saturn_holder);
  root -> addChildren(saturn_holder);

  GeometryNode uranus (planet_model);
  auto uranus_holder = std::make_shared<Node>(uranus);
  uranus_holder -> setName("uranus");
  uranus_holder -> setParent(root);
  uranus_holder -> setDistance(33.0f); //distance to origin
  uranus_holder -> setSpeed(0.15f); //rotation speed
  uranus_holder -> setSize(0.75f);  //scale size
  uranus_holder -> setParent(uranus_holder);
  root -> addChildren(uranus_holder);

  GeometryNode neptun (planet_model);
  auto neptun_holder = std::make_shared<Node>(neptun);
  neptun_holder -> setName("neptun");
  neptun_holder -> setParent(root);
  neptun_holder -> setDistance(40.0f); //distance to origin
  neptun_holder -> setSpeed(0.1f); //rotation speed
  neptun_holder -> setSize(0.75f);  //scale size
  neptun_holder -> setParent(neptun_holder);
  root -> addChildren(neptun_holder);

  GeometryNode sun (planet_model);
  auto sun_holder = std::make_shared<Node>(sun);
  sun_holder -> setName("sun");
  sun_holder -> setParent(root);
  sun_holder -> setDistance(0.0f); //distance to origin
  sun_holder -> setSpeed(0.1f); //rotation speed
  sun_holder -> setSize(2.5f);  //scale size
  sun_holder -> setParent(sun_holder);
  sun_holder -> setPlanetColor(glm::vec3{1.0, 5.0, 1.0});
  root -> addChildren(sun_holder);

   GeometryNode earth (planet_model);
  auto earth_holder = std::make_shared<Node>(earth);
  earth_holder -> setName("earth");
  earth_holder -> setParent(root);
  earth_holder -> setDistance(12.0f); //distance to origin
  earth_holder -> setSpeed(0.45f); //rotation speed
  earth_holder -> setSize(0.5f);  //scale size
  earth_holder -> setParent(earth_holder);
  root -> addChildren(earth_holder); 
  
  GeometryNode moon (planet_model);
  auto moon_holder = std::make_shared<Node>(moon);
  moon_holder -> setName("moon");
  moon_holder -> setParent(earth_holder);
  moon_holder -> setDistance(5.0f); //distance to origin
  moon_holder -> setSpeed(0.9f); //rotation speed
  moon_holder -> setSize(1.5f); //scale size 
  moon_holder -> setParent(earth_holder);
  earth_holder -> addChildren(moon_holder); 


  // camera information in scene graph
  auto camera = std::make_shared<CameraNode>();
  camera -> setParent(root);
  camera -> setName("camera");
  root -> addChildren(camera);

  // initialize the new scene graph
  SceneGraph sceneGraph_tmp{"SolarSystem", root};
  scene_graph = sceneGraph_tmp;
}

// 3-component floating-point value.
struct float3{
  float3( float _x = 0.0f, float _y = 0.0f, float _z = 0.0f ): 
    x(_x), 
    y(_y), 
    z(_z) 
    {}
     
    float x;
    float y;
    float z;
};

// Vertex definition
struct VertexXYZColor{
// https://www.3dgep.com/rendering-primitives-with-opengl/#Primitivess
    float3 m_Pos;
    float3 m_Color;
};
 
int random(int lowerbounds, int upperbounds){
// https://www.c-plusplus.net/forum/topic/172876/zuf%C3%A4lle-gibt-s-funktionen-rund-um-rand-random-und-den-zufall

  return lowerbounds + std::rand() % (upperbounds - lowerbounds + 1);
}


void ApplicationSolar::initializeStars(){

  std::vector<float> star_container;
  int number_of_stars = 1000;

  VertexXYZColor g_Vertices[1000 /*number_of_stars*/];

  float x, y, z = 0;
  float r, g, b = 0;
  
  for(int i = 0; i < number_of_stars; i++){
    x = (float)(random(0, 100)) - 50.0f; 
    y = (float)(random(0, 100)) - 50.0f;
    z = (float)(-(random(0, 100))) - 50.0f;

    r = (float) random(0,255);
    g = (float) random(0,255);
    b = (float) random(0,255);

    auto position = float3(x,y,z);
    auto color = float3(r,g,b);

    g_Vertices[i]= {position,color};
  }

  // initialize vertex array object (vao)
  glGenVertexArrays(1, &star_object.vertex_AO);
  glBindVertexArray(star_object.vertex_AO);

  // initialize vertex buffer object (vbo) and load data
  glGenBuffers(1, &star_object.vertex_BO);
  glBindBuffer(GL_ARRAY_BUFFER, star_object.vertex_BO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * number_of_stars * 6, g_Vertices /*star_container.data()*/, GL_STATIC_DRAW);

  // specify (activate, connect and set format) the attributes
  glEnableVertexAttribArray(0);
  // first attribute is 3 floats with no offset & stride
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, GLsizei(sizeof(float)*6), 0);
  // activate second attribute on gpu
  glEnableVertexAttribArray(1);
  // second attribute is 3 floats with no offset & stride
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, GLsizei(sizeof(float)*6), (void*) (sizeof(float)*3));

  // define vertex indices (optional)
  // generate generic buffer
  glGenBuffers(1, &star_object.element_BO);
  // bind this as an vertex array buffer containing all attributes
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, star_object.element_BO);
  // configure currently bound array buffer
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(float) * number_of_stars * 6, g_Vertices, GL_STATIC_DRAW);

  // store type of primitive to draw
  star_object.draw_mode = GL_POINTS ;
  // transfer number of indices to model object 
  star_object.num_elements = GLsizei(number_of_stars);

}

// load models
void ApplicationSolar::initializeGeometry() {
  model planet_model = model_loader::obj(m_resource_path + "models/sphere.obj", model::NORMAL);

  // generate vertex array object
  glGenVertexArrays(1, &planet_object.vertex_AO);
  // bind the array for attaching buffers
  glBindVertexArray(planet_object.vertex_AO);

  // generate generic buffer
  glGenBuffers(1, &planet_object.vertex_BO);
  // bind this as an vertex array buffer containing all attributes
  glBindBuffer(GL_ARRAY_BUFFER, planet_object.vertex_BO);
  // configure currently bound array buffer
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * planet_model.data.size(), planet_model.data.data(), GL_STATIC_DRAW);

  // activate first attribute on gpu
  glEnableVertexAttribArray(0);
  // first attribute is 3 floats with no offset & stride
  glVertexAttribPointer(0, model::POSITION.components, model::POSITION.type, GL_FALSE, planet_model.vertex_bytes, planet_model.offsets[model::POSITION]);
  // activate second attribute on gpu
  glEnableVertexAttribArray(1);
  // second attribute is 3 floats with no offset & stride
  glVertexAttribPointer(1, model::NORMAL.components, model::NORMAL.type, GL_FALSE, planet_model.vertex_bytes, planet_model.offsets[model::NORMAL]);

   // generate generic buffer
  glGenBuffers(1, &planet_object.element_BO);
  // bind this as an vertex array buffer containing all attributes
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, planet_object.element_BO);
  // configure currently bound array buffer
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, model::INDEX.size * planet_model.indices.size(), planet_model.indices.data(), GL_STATIC_DRAW);

  // store type of primitive to draw
  planet_object.draw_mode = GL_TRIANGLES;
  // transfer number of indices to model object 
  planet_object.num_elements = GLsizei(planet_model.indices.size());
}

///////////////////////////// callback functions for window events ////////////
// handle key input
void ApplicationSolar::keyCallback(int key, int action, int mods) {
  //zoom in
  if (key == GLFW_KEY_W  && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    m_view_transform = glm::translate(m_view_transform, glm::fvec3{0.0f, 0.0f, -0.1f});
  }
  //zoom out
  else if (key == GLFW_KEY_S  && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    m_view_transform = glm::translate(m_view_transform, glm::fvec3{0.0f, 0.0f, 0.1f});
  }
  //up
  else if (key == GLFW_KEY_UP  && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    m_view_transform = glm::translate(m_view_transform, glm::fvec3{0.0f, -0.1f, 0.0f});
  }
  //down
  else if (key == GLFW_KEY_DOWN  && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    m_view_transform = glm::translate(m_view_transform, glm::fvec3{0.0f, 0.1f, 0.0f});
  }
  //left
  else if (key == GLFW_KEY_LEFT  && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    m_view_transform = glm::translate(m_view_transform, glm::fvec3{0.1f, 0.0f, 0.0f});
  }
  //right
  else if (key == GLFW_KEY_RIGHT  && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    m_view_transform = glm::translate(m_view_transform, glm::fvec3{-0.1f, 0.0f, 0.0f});
  }
  uploadView();

}

//handle delta mouse movement input
void ApplicationSolar::mouseCallback(double pos_x, double pos_y) {
  // mouse handling
  /*/left
  if(pos_x > 0){
    m_view_transform = glm::translate(m_view_transform, glm::fvec3{-0.1f, 0.0f, 0.0f});
    uploadView();
  }
  //right
  if(pos_x < 0){
    m_view_transform = glm::translate(m_view_transform, glm::fvec3{0.1f, 0.0f, 0.0f});
    uploadView();
  }
  //down
  if(pos_y > 0){
    m_view_transform = glm::translate(m_view_transform, glm::fvec3{0.0f, 0.1f, 0.0f});
    uploadView();
  }
  //up
  if(pos_y < 0){
    m_view_transform = glm::translate(m_view_transform, glm::fvec3{0.0f, -0.1f, 0.0f});
    uploadView();Assignment3ShadersFrancesco Andreussifrancesco.andreussi@uni–weimar.de12 December 2019DeadlineWednesday, 9 January 2020 at 23:55.Task•Extend theNodeclass with aPointLightNodeclass, which has as additional at-tributeslightIntensityandlightColor.  (10%)•Assign a different color for each planet usingglUniform3f
  }*/

    float horizontal_rotate = float(pos_x/20); //divide to slow down
    float vertical_rotate = float(pos_y/20);

  m_view_transform = glm::rotate(m_view_transform, glm::radians(vertical_rotate), glm::fvec3{1.0f,0.0f,0.0f});
  m_view_transform = glm::rotate(m_view_transform, glm::radians(horizontal_rotate), glm::fvec3{0.0f,1.0f,0.0f});
  
}

//handle resizing
void ApplicationSolar::resizeCallback(unsigned width, unsigned height) {
  // recalculate projection matrix for new aspect ration
  m_view_projection = utils::calculate_projection_matrix(float(width) / float(height));
  // upload new projection matrix
  uploadProjection();
}


// exe entry point
int main(int argc, char* argv[]) {
  Application::run<ApplicationSolar>(argc, argv, 3, 2);
}