#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/RenderTarget.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>

#include <utils/EntityManager.h>
#include <utils/Entity.h>

#include <utils/Path.h>

#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>
#include <filamentapp/NativeWindowHelper.h>

#include <stb_image.h>

#include <iostream> // for cerr

#include "generated/resources/resources.h"
//#include <SDL.h>

#ifdef main
#undef main
#endif

using namespace filament;
using utils::Entity;
using utils::EntityManager;
using utils::Path;
using MinFilter = TextureSampler::MinFilter;
using MagFilter = TextureSampler::MagFilter;

struct Vertex {
    filament::math::float2 position;
    filament::math::float2 uv;
    filament::math::float4 color;
};

static const Vertex QUAD_VERTICES[4] = {
    {{-1, -1}, {0, 0}, {0, 0, 0, 0}},
    {{ 1, -1}, {1, 0}, {0, 0, 0, 0}},
    {{-1,  1}, {0, 1}, {0, 0, 0, 0}},
    {{ 1,  1}, {1, 1}, {0, 0, 0, 0}},
};

static const Vertex TRIANGLE_VERTICES[3] = {
    {{-0.5, -0.5}, {0, 0}, {1, 0, 0, 1}},
    {{ 0.5, -0.5}, {0, 0}, {0, 1, 0, 1}},
    {{   0,  0.5}, {0, 0}, {0, 0, 1, 1}},
};

static constexpr uint16_t QUAD_INDICES[6] = {
    0, 1, 2,
    3, 2, 1,
};

static constexpr uint16_t TRIANGLE_INDICES[3] = {
    0, 1, 2   
};

constexpr uint8_t BAKED_TEXTURE_MAT[] = {
#include "bakedTexture.inc"
};

constexpr uint8_t BAKED_COLOR_MAT[] = {
#include "bakedColorAlpha.inc"
};

constexpr char kTitleName[] = "multiple fbos";
constexpr int kWindowPosX = SDL_WINDOWPOS_CENTERED;
constexpr int kWindowPosY = SDL_WINDOWPOS_CENTERED;
constexpr uint32_t kWindowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;

static bool ShouldWindowExit(SDL_Event* event){
    SDL_PollEvent(event);
    if(event->type == SDL_KEYDOWN){
        int key = event->key.keysym.scancode;
        return (key == 41); //esc pressed
    }
    return false;
}

static void FreeCallback(void* buffer, size_t size, void* user){
    stbi_image_free(buffer);
}

class TextureWrapper{
public:    
    TextureWrapper(filament::Engine *engine):
        engine_(engine) {}

    TextureWrapper(filament::Engine *engine, uint32_t w, uint32_t h, const void* data,
        Texture::PixelBufferDescriptor::Callback cbk = nullptr): engine_(engine){
        GenerateTexture2D(w, h, data, cbk);
    }

    ~TextureWrapper(){ Destroy(); }
    
    filament::Texture const* texture_ref() {return texture_;}

    void GenerateTexture2D(uint32_t w, uint32_t h, const void* data, Texture::PixelBufferDescriptor::Callback cbk = nullptr){
        Texture::PixelBufferDescriptor buff_desc(data, w * h * 4, Texture::Format::RGBA, Texture::Type::UBYTE, cbk);
        texture_ = Texture::Builder()
            .width(w)
            .height(h)
            .levels(1)
            .sampler(Texture::Sampler::SAMPLER_2D)
            .format(Texture::InternalFormat::RGBA8)
            .build(*engine_);
        
        texture_->setImage(*engine_, 0, std::move(buff_desc));
    }

    void Destroy(){
        if(!texture_ || !engine_) return;
        engine_->destroy(texture_);
        texture_ = nullptr;
        engine_  = nullptr;
    }

private:
    filament::Texture *texture_ = nullptr;
    filament::Engine  *engine_  = nullptr;
};

struct FilamentEntity{
    VertexBuffer *vb = nullptr;
    IndexBuffer *ib = nullptr;
    Material *mat = nullptr;    
    MaterialInstance *mat_inst = nullptr;
    Entity entity;
    Engine *engine = nullptr;
    
    FilamentEntity() = default;
    FilamentEntity(Engine *_engine) : engine(_engine){}
    
    void Destroy(){
        if(!engine) return;
        engine->destroy(vb);
        engine->destroy(ib);
        engine->destroy(mat_inst);
        engine->destroy(mat);
        engine->destroy(entity);
    }
    
    operator const Entity&() const{return entity;}
    
    void InitVertexBuffer(uint32_t nvertices, uint8_t byte_stride, uint32_t pos_offset, uint32_t uv_offset, uint32_t color_offset, const void *data){
        vb = VertexBuffer::Builder()
            .vertexCount(nvertices)
            .bufferCount(1)
            .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT2, pos_offset, byte_stride)
            .attribute(VertexAttribute::UV0, 0, VertexBuffer::AttributeType::FLOAT2, uv_offset, byte_stride)
            .attribute(VertexAttribute::COLOR, 0, VertexBuffer::AttributeType::FLOAT4, color_offset, byte_stride)
            .build(*engine);
        
        vb->setBufferAt(*engine, 0, VertexBuffer::BufferDescriptor(data, nvertices * byte_stride, nullptr));
    }
    
    //默认索引类型为ushort
    void InitIndexBuffer(uint32_t nindices, const void* data){
        ib = IndexBuffer::Builder()
            .indexCount(nindices)
            .bufferType(IndexBuffer::IndexType::USHORT)
            .build(*engine);
        
        ib->setBuffer(*engine, IndexBuffer::BufferDescriptor(data, 2 * nindices, nullptr));
    }

    template <typename T>
    void SetUniform(const char* name, T value, const TextureSampler* sampler = nullptr){
        if(!mat_inst) return;
        mat_inst->setParameter(name, value);
    }

    void BindTextureSampler(const char* sampler_name, const filament::Texture* tex){
        if(tex && mat_inst){
            TextureSampler sampler(MinFilter::LINEAR, MagFilter::LINEAR, TextureSampler::WrapMode::MIRRORED_REPEAT);
            mat_inst->setParameter(sampler_name, tex, sampler);
        }
    }
    
    void InitMaterial(const void* material_data, size_t size){
        mat = Material::Builder()
            .package(material_data, size)
            .build(*engine);
        mat_inst = mat->createInstance();

        entity = EntityManager::get().create();
        RenderableManager::Builder(1)
            .boundingBox({{ -1, -1, -1 }, { 1, 1, 1 }})
            .material(0, mat_inst)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, ib)
            .culling(false)
            .receiveShadows(false)
            .castShadows(false)
            .build(*engine, entity);
    }
    
};

class FilaRenderTarget {
public:
    FilaRenderTarget(){}    
    ~FilaRenderTarget(){}

    operator filament::RenderTarget*() const{
        return target_;
    }
    
    void ResetRenderBuffer(int width, int height, filament::Engine* engine, bool need_depth = false){
        Destroy(engine);

        color_ = filament::Texture::Builder()
            .width(width)
            .height(height)
            .levels(1)
            .sampler(filament::Texture::Sampler::SAMPLER_2D)
            .format(filament::Texture::InternalFormat::RGBA8)
            .usage(filament::Texture::Usage::COLOR_ATTACHMENT | filament::Texture::Usage::SAMPLEABLE)
            .build(*engine);

        if(!need_depth){
            target_ = filament::RenderTarget::Builder()
                .texture(filament::RenderTarget::AttachmentPoint::COLOR, color_)
                .mipLevel(filament::RenderTarget::AttachmentPoint::COLOR, 0)
                .build(*engine);
        }else{
            depth_ = filament::Texture::Builder()
                .width(width)
                .height(height)
                .sampler(filament::Texture::Sampler::SAMPLER_2D)
                .format(filament::Texture::InternalFormat::DEPTH24_STENCIL8)
                .usage(filament::Texture::Usage::DEPTH_ATTACHMENT)
                .build(*engine);

            target_ = filament::RenderTarget::Builder()
                .texture(filament::RenderTarget::AttachmentPoint::COLOR, color_)
                .mipLevel(filament::RenderTarget::AttachmentPoint::COLOR, 0)
                .texture(filament::RenderTarget::AttachmentPoint::DEPTH, depth_)
                .mipLevel(filament::RenderTarget::AttachmentPoint::DEPTH, 0)
                .build(*engine);
        }
    }

    void Destroy(filament::Engine* engine){
        if(color_ && target_){
            engine->destroy(color_);
            engine->destroy(target_);
            color_ = nullptr;
            target_ = nullptr;
        }

        if(depth_){
            engine->destroy(depth_);
            depth_ = nullptr;
        }
    } 

    filament::Texture *color() {return color_;}
    filament::Texture *depth() {return depth_;}   

private:
    filament::RenderTarget *target_ = nullptr;
    filament::Texture *color_ = nullptr;
    filament::Texture *depth_ = nullptr;
};


class Renderable {
public:
    Renderable(filament::Engine* engine, const std::string& name, bool enable_postprocessing = false){
        engine_ = engine;
        if(engine_){
            //skybox_ = filament::Skybox::Builder()
            //    .color({1.0, 0.0, 1.0, 0.5})
            //    .build(*engine_);
            cam_ent_ = utils::EntityManager::get().create();
            scene_  = engine_->createScene();
            view_   = engine_->createView();
            camera_ = engine->createCamera(cam_ent_);
            scene_->setSkybox(skybox_);
            view_->setCamera(camera_);
            view_->setScene(scene_);
            view_->setPostProcessingEnabled(enable_postprocessing);            
            name_ = name;
        }
    }

    Renderable(filament::Engine* engine, const std::string& name, FilaRenderTarget& target, bool enable_postprocessing = false){
        engine_ = engine;
        if(engine_) {
            //skybox_ = filament::Skybox::Builder()
            //    .color({1.0, 0.0, 1.0, 0.5})
            //    .build(*engine_);
            cam_ent_ = utils::EntityManager::get().create();
            scene_  = engine_->createScene();            
            view_   = engine_->createView();
            camera_ = engine->createCamera(cam_ent_);
            scene_->setSkybox(skybox_);
            view_->setPostProcessingEnabled(enable_postprocessing);
            view_->setCamera(camera_);
            view_->setScene(scene_);
            view_->setRenderTarget(target);
            view_->setBlendMode(View::BlendMode::TRANSLUCENT);
            name_ = name;
        }
    }

    void SetViewport(int32_t x, int32_t y, uint32_t w, uint32_t h){
        view_->setViewport({x, y, w, h});
    }

    ~Renderable(){ Destroy(); }

    operator filament::View*() const { return view_; }   

    void AddEntity(const char* name, FilamentEntity *entity){
        scene_->addEntity(*entity);
        entities_.emplace(name_ + "_" + name, entity);
    }

    void Destroy(){
        if(!engine_) return;
        
        for(auto it = entities_.begin(); it != entities_.end();){
            it->second->Destroy();
            it = entities_.erase(it);
        }

        engine_->destroy(skybox_);
        engine_->destroyCameraComponent(cam_ent_);
        utils::EntityManager::get().destroy(cam_ent_);
        engine_->destroy(view_);
        engine_->destroy(scene_);
        engine_ = nullptr;
    }

private:
    filament::Scene   *scene_ = nullptr;    
    filament::View     *view_ = nullptr;
    filament::Camera *camera_ = nullptr;
    filament::Engine *engine_ = nullptr;
    filament::Skybox *skybox_ = nullptr;
    utils::Entity cam_ent_;

    std::unordered_map<std::string, FilamentEntity*> entities_;
    std::string name_;
};

class FilamentRenderer{
public:
    friend class ::RenderTarget;
public:
    FilamentRenderer() = default;
    FilamentRenderer(Engine::Backend backend, void *native_window){
        Init(backend, native_window);
    }
    
    ~FilamentRenderer(){
        if(engine_)
            Destroy();
    }
    
    void Init(Engine::Backend backend, void *native_window){
        engine_ = Engine::create(backend);
        swapchain_ = engine_->createSwapChain(native_window);
        renderer_ = engine_->createRenderer();
        
        renderer_->setClearOptions({{0.0, 0.0, 0.0, 0.0}, true});
    }   
    
    void Destroy(){
        engine_->destroy(swapchain_);
        engine_->destroy(renderer_);
        engine_->destroy(&engine_);
        engine_ = nullptr;
    }       

    void DoRender(const std::vector<const ::Renderable*> &renderables){        
        if(renderer_->beginFrame(swapchain_)){
            for(const auto& renderable : renderables){                
                renderer_->render(*renderable);
                //int w = renderable->color()->getWidth();
                //int h = renderable->color()->getHeight();
                //size_t nbytes = w * h * 4;
                //uint8_t *buff = new uint8_t[nbytes];
                //backend::PixelBufferDescriptor pb_desc(buff, nbytes, backend::PixelDataFormat::RGBA,
                //                                       backend::PixelDataType::UBYTE, nullptr, nullptr);
                //renderer_->readPixels(*renderable, 0, 0, w, h, std::move(pb_desc));
                //engine_->flushAndWait();

                //FILE* f = fopen("image.rgba", "wb");
                //fwrite(buff, 1, nbytes, f);
                //fclose(f);
                //delete []buff;
            }
            engine_->flushAndWait();
            renderer_->endFrame();
        }
    }
    
    Engine *engine_ref() const {return engine_;}
    
private:    
    Engine *engine_ = nullptr;
    SwapChain *swapchain_ = nullptr ;
    Renderer *renderer_= nullptr;        
};

int main(){
    if(SDL_Init(SDL_INIT_EVENTS) !=0 ){
        std::cout<< "SDL init failed! exit..." <<std::endl;
        return -1;
    }
    
    bool open_post_process = false;
    
    //load texture data
    Path path = FilamentApp::getRootAssetsPath() + "textures/Moss_01/Moss_01_Color.png";
    if (!path.exists()) {
        std::cerr << "The texture " << path << " does not exist" << std::endl;
        exit(1);
    }
    int width, height, nchannels;
    uint8_t *data = stbi_load(path.c_str(), &width, &height, &nchannels, 4);
    if (data == nullptr) {
        std::cerr << "The texture " << path << " could not be loaded" << std::endl;
        exit(1);
    }
    std::cout << "Loaded texture: " << width << "x" << height << ", channels: "<<nchannels<<std::endl;
    
    //create window
    SDL_Window *window = SDL_CreateWindow(kTitleName, kWindowPosX, kWindowPosY, width, height, kWindowFlags);
    SDL_Event event;
        
    FilamentRenderer renderer(Engine::Backend::OPENGL, getNativeWindow(window));
    FilamentEntity entity_texture(renderer.engine_ref()); 
    FilamentEntity entity_display(renderer.engine_ref());

    TextureWrapper tex_wrapper(renderer.engine_ref(), width, height, data);

    uint8_t byte_stride = sizeof(Vertex);
    uint32_t pos_offset = offsetof(Vertex, position);
    uint32_t uv_offset  = offsetof(Vertex, uv);
    uint32_t color_offset  = offsetof(Vertex, color);

    //simple texture(offscreen)
    uint32_t nvertices = sizeof(QUAD_VERTICES) / byte_stride;
    uint32_t nindices  = sizeof(QUAD_INDICES)  / sizeof(uint16_t);
    entity_texture.InitVertexBuffer(nvertices, byte_stride, pos_offset, uv_offset, color_offset,  QUAD_VERTICES);
    entity_texture.InitIndexBuffer(nindices, QUAD_INDICES);
    entity_texture.InitMaterial((void*)BAKED_TEXTURE_MAT, sizeof(BAKED_TEXTURE_MAT));    
    entity_texture.BindTextureSampler("albedo", tex_wrapper.texture_ref());

    FilaRenderTarget rgba_target;
    rgba_target.ResetRenderBuffer(width, height, renderer.engine_ref(), true);
    ::Renderable renderable_texture(renderer.engine_ref(), "SimpleTexture", rgba_target);
    renderable_texture.SetViewport(0, 0, width, height);
    renderable_texture.AddEntity("Quad", &entity_texture);

    //simple texture(onscreen)
    entity_display.InitVertexBuffer(nvertices, byte_stride, pos_offset, uv_offset, color_offset,  QUAD_VERTICES);
    entity_display.InitIndexBuffer(nindices, QUAD_INDICES);
    entity_display.InitMaterial((void*)BAKED_TEXTURE_MAT, sizeof(BAKED_TEXTURE_MAT));    
    entity_display.BindTextureSampler("albedo", rgba_target.color());

    ::Renderable renderable_display(renderer.engine_ref(), "Display"); 
    renderable_display.SetViewport(0, 0, width, height);
    renderable_display.AddEntity("Quad", &entity_display);

    //simple triangle(offscreen)
    nvertices = sizeof(TRIANGLE_VERTICES) / byte_stride;
    nindices  = sizeof(TRIANGLE_INDICES) / sizeof(uint16_t);
    FilamentEntity entity_triangle(renderer.engine_ref());
    entity_triangle.InitVertexBuffer(nvertices, byte_stride, pos_offset, uv_offset, color_offset,  TRIANGLE_VERTICES);
    entity_triangle.InitIndexBuffer(nindices, TRIANGLE_INDICES);
    entity_triangle.InitMaterial((void*)BAKED_COLOR_MAT, sizeof(BAKED_COLOR_MAT));    

    ::Renderable renderable_triangle(renderer.engine_ref(), "Triangle", rgba_target, open_post_process);
    renderable_triangle.SetViewport(0, 0, width, height);
    renderable_triangle.AddEntity("Triangle", &entity_triangle);
    
    while(!ShouldWindowExit(&event)){        
        renderer.DoRender({&renderable_texture, &renderable_triangle, &renderable_display});
    }
    
    stbi_image_free(data);
    rgba_target.Destroy(renderer.engine_ref());
    tex_wrapper.Destroy();
    renderable_texture.Destroy();    
    renderable_display.Destroy();
    renderable_triangle.Destroy();
    renderer.Destroy();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
