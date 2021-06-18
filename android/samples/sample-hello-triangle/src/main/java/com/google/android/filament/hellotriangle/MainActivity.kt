/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.google.android.filament.hellotriangle

import android.animation.ValueAnimator
import android.app.Activity
import android.opengl.Matrix
import android.os.Bundle
import android.util.Log
import android.view.Choreographer
import android.view.Surface
import android.view.SurfaceView
import android.view.animation.LinearInterpolator
import com.google.android.filament.*
import com.google.android.filament.RenderableManager.PrimitiveType
import com.google.android.filament.VertexBuffer.AttributeType
import com.google.android.filament.VertexBuffer.VertexAttribute
import com.google.android.filament.android.DisplayHelper
import com.google.android.filament.android.UiHelper
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.Channels
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

class MainActivity : Activity() {
    // Make sure to initialize Filament first
    // This loads the JNI library needed by most API calls
    companion object {
        init {
            Filament.init()
        }
    }
    private val TAG = MainActivity::class.qualifiedName
    private var texWidth : Int = 0
    private var texHeight : Int = 0
    // The View we want to render into
    private lateinit var surfaceView: SurfaceView
    // UiHelper is provided by Filament to manage SurfaceView and SurfaceTexture
    private lateinit var uiHelper: UiHelper
    // DisplayHelper is provided by Filament to manage the display
    private lateinit var displayHelper: DisplayHelper
    // Choreographer is used to schedule new frames
    private lateinit var choreographer: Choreographer

    // Engine creates and destroys Filament resources
    // Each engine must be accessed from a single thread of your choosing
    // Resources cannot be shared across engines
    private lateinit var engine: Engine
    // A renderer instance is tied to a single surface (SurfaceView, TextureView, etc.)
    private lateinit var renderer: Renderer
    // A scene holds all the renderable, lights, etc. to be drawn
    private lateinit var renderedScene: Scene
    private lateinit var postProcessScene: Scene
    // A view defines a viewport, a renderedScene and a camera for rendering
    private lateinit var renderedView: View
    private lateinit var postProcessView: View
    // Should be pretty obvious :)
    private lateinit var camera: Camera
    //target which will be rendered into
    private lateinit var renderedTarget: RenderTarget
    private lateinit var renderedColorTex: Texture
    private lateinit var renderedDepthTex: Texture
    //target for image post-processing
//    private lateinit var postProcessTarget: RenderTarget
//    private lateinit var postProcessColorTex: Texture

    private lateinit var renderedMaterial: Material
    private lateinit var renderedVertexBuffer: VertexBuffer
    private lateinit var renderedIndexBuffer: IndexBuffer

    private lateinit var postProcessMaterial: Material
    private lateinit var postProcessVertexBuffer: VertexBuffer
    private lateinit var postProcessIndexBuffer: IndexBuffer

    // Filament entity representing a renderable object
    @Entity private var renderedRenderable = 0
    @Entity private var postProcessRenderable = 0

    // A swap chain is Filament's representation of a surface
    private var swapChain: SwapChain? = null

    // Performs the rendering and schedules new frames
    private val frameScheduler = FrameCallback()

    private val animator = ValueAnimator.ofFloat(0.0f, 360.0f)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        surfaceView = SurfaceView(this)
        setContentView(surfaceView)

        choreographer = Choreographer.getInstance()

        displayHelper = DisplayHelper(this)

        setupSurfaceView()
        setupFilament()
        setupView()
        setupScene()
    }

    private fun setupSurfaceView() {
        uiHelper = UiHelper(UiHelper.ContextErrorPolicy.DONT_CHECK)
        uiHelper.renderCallback = SurfaceCallback()

        // NOTE: To choose a specific rendering resolution, add the following line:
        // uiHelper.setDesiredSize(1280, 720)
        uiHelper.attachTo(surfaceView)
    }

    private fun setupFilament() {
        engine = Engine.create()
        renderer = engine.createRenderer()
        renderedScene = engine.createScene()
        renderedView = engine.createView()
        postProcessScene = engine.createScene()
        postProcessView = engine.createView()
        camera = engine.createCamera()
    }

    private fun setupView() {
        renderedScene.skybox = Skybox.Builder().color(0.035f, 0.035f, 0.035f, 1.0f).build(engine)

        // NOTE: Try to disable post-processing (tone-mapping, etc.) to see the difference
        // view.isPostProcessingEnabled = false

        // Tell the renderedView which camera we want to use
        renderedView.camera = camera

        // Tell the view which scene we want to render
        renderedView.scene = renderedScene

        postProcessView.camera = camera
        postProcessView.scene  = postProcessScene
    }

    private fun setupScene() {
        loadMaterial()
        createMesh()

        // To create a renderable we first create a generic entity
        renderedRenderable = EntityManager.get().create()

        // We then create a renderable component on that entity
        // A renderable is made of several primitives; in this case we declare only 1
        RenderableManager.Builder(1)
                // Overall bounding box of the renderable
                .boundingBox(Box(-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f))
                // Sets the mesh data of the first primitive
                .geometry(0, PrimitiveType.TRIANGLES, renderedVertexBuffer, renderedIndexBuffer, 0, 3)
                // Sets the material of the first primitive
                .material(0, renderedMaterial.defaultInstance)
                .build(engine, renderedRenderable)

        // Add the entity to the scene to render it
        renderedScene.addEntity(renderedRenderable)

        postProcessRenderable = EntityManager.get().create()

        RenderableManager.Builder(1)
                // Overall bounding box of the renderable
                .boundingBox(Box(-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f))
                // Sets the mesh data of the first primitive
                .geometry(0, PrimitiveType.TRIANGLES, postProcessVertexBuffer, postProcessIndexBuffer, 0, 6)
                // Sets the material of the first primitive
                .material(0, postProcessMaterial.defaultInstance)
                .build(engine, postProcessRenderable)

        postProcessScene.addEntity(postProcessRenderable)

        //startAnimation()
    }

    private fun loadMaterial() {
        readUncompressedAsset("materials/baked_color.filamat").let {
            renderedMaterial = Material.Builder().payload(it, it.remaining()).build(engine)
        }

        readUncompressedAsset("materials/baked_color_negative.filamat").let {
            postProcessMaterial = Material.Builder().payload(it, it.remaining()).build(engine)
        }
    }

    private fun createMesh() {
        val intSize = 4
        val floatSize = 4
        val shortSize = 2
        // A vertex is a position + a color:
        // 3 floats for XYZ position, 1 integer for color
        val vertexSize = 3 * floatSize + intSize

        // Define a vertex and a function to put a vertex in a ByteBuffer
        data class Vertex(val x: Float, val y: Float, val z: Float, val color: Int)
        fun ByteBuffer.put(v: Vertex): ByteBuffer {
            putFloat(v.x)
            putFloat(v.y)
            putFloat(v.z)
            putInt(v.color)
            return this
        }

        // We are going to generate a single triangle
        val vertexCount = 3
        val a1 = PI * 2.0 / 3.0
        val a2 = PI * 4.0 / 3.0

        val vertexData = ByteBuffer.allocate(vertexCount * vertexSize)
                // It is important to respect the native byte order
                .order(ByteOrder.nativeOrder())
                .put(Vertex(1.0f,              0.0f,              0.0f, 0xffff0000.toInt()))
                .put(Vertex(cos(a1).toFloat(), sin(a1).toFloat(), 0.0f, 0xff00ff00.toInt()))
                .put(Vertex(cos(a2).toFloat(), sin(a2).toFloat(), 0.0f, 0xff0000ff.toInt()))
                // Make sure the cursor is pointing in the right place in the byte buffer
                .flip()

        // Declare the layout of our mesh
        renderedVertexBuffer = VertexBuffer.Builder()
                .bufferCount(1)
                .vertexCount(vertexCount)
                // Because we interleave position and color data we must specify offset and stride
                // We could use de-interleaved data by declaring two buffers and giving each
                // attribute a different buffer index
                .attribute(VertexAttribute.POSITION, 0, AttributeType.FLOAT3, 0,             vertexSize)
                .attribute(VertexAttribute.COLOR,    0, AttributeType.UBYTE4, 3 * floatSize, vertexSize)
                // We store colors as unsigned bytes but since we want values between 0 and 1
                // in the material (shaders), we must mark the attribute as normalized
                .normalized(VertexAttribute.COLOR)
                .build(engine)

        // Feed the vertex data to the mesh
        // We only set 1 buffer because the data is interleaved
        renderedVertexBuffer.setBufferAt(engine, 0, vertexData)

        // Create the indices
        val indexData = ByteBuffer.allocate(vertexCount * shortSize)
                .order(ByteOrder.nativeOrder())
                .putShort(0)
                .putShort(1)
                .putShort(2)
                .flip()

        renderedIndexBuffer = IndexBuffer.Builder()
                .indexCount(3)
                .bufferType(IndexBuffer.Builder.IndexType.USHORT)
                .build(engine)
        renderedIndexBuffer.setBuffer(engine, indexData)


        data class PostProcessVertex(val x: Float, val y: Float, val u: Float, val v: Float)
        fun ByteBuffer.put(v: PostProcessVertex): ByteBuffer {
            putFloat(v.x)
            putFloat(v.y)
            putFloat(v.u)
            putFloat(v.v)
            return this
        }
        val postProcessVertexSize = 4 * floatSize;
        val postProcessVertexCount = 4;
        val postProcessVertexData = ByteBuffer.allocate(postProcessVertexCount * postProcessVertexSize)
                // It is important to respect the native byte order
                .order(ByteOrder.nativeOrder())
                .put(PostProcessVertex(-1.0f, -1.0f, 0.0f, 0.0f))
                .put(PostProcessVertex(1.0f, -1.0f, 1.0f, 0.0f))
                .put(PostProcessVertex(-1.0f, 1.0f, 0.0f, 1.0f))
                .put(PostProcessVertex(1.0f, 1.0f, 1.0f, 1.0f))
                // Make sure the cursor is pointing in the right place in the byte buffer
                .flip()

        postProcessVertexBuffer = VertexBuffer.Builder()
                .bufferCount(1)
                .vertexCount(postProcessVertexCount)
                // Because we interleave position and color data we must specify offset and stride
                // We could use de-interleaved data by declaring two buffers and giving each
                // attribute a different buffer index
                .attribute(VertexAttribute.POSITION, 0, AttributeType.FLOAT2, 0,             postProcessVertexSize)
                .attribute(VertexAttribute.UV0,    0, AttributeType.FLOAT2, 2 * floatSize, postProcessVertexSize)
                .build(engine)

        postProcessVertexBuffer.setBufferAt(engine,0, postProcessVertexData)

        val postProcessIndexData = ByteBuffer.allocate(6 * shortSize)
                .order(ByteOrder.nativeOrder())
                .putShort(0)
                .putShort(1)
                .putShort(2)
                .putShort(3)
                .putShort(2)
                .putShort(1)
                .flip()

        postProcessIndexBuffer = IndexBuffer.Builder()
                .indexCount(6)
                .bufferType(IndexBuffer.Builder.IndexType.USHORT)
                .build(engine)
        postProcessIndexBuffer.setBuffer(engine, postProcessIndexData)


    }

    private fun startAnimation() {
        // Animate the triangle
        animator.interpolator = LinearInterpolator()
        animator.duration = 4000
        animator.repeatMode = ValueAnimator.RESTART
        animator.repeatCount = ValueAnimator.INFINITE
        animator.addUpdateListener(object : ValueAnimator.AnimatorUpdateListener {
            val transformMatrix = FloatArray(16)
            override fun onAnimationUpdate(a: ValueAnimator) {
                Matrix.setRotateM(transformMatrix, 0, -(a.animatedValue as Float), 0.0f, 0.0f, 1.0f)
                val tcm = engine.transformManager
                tcm.setTransform(tcm.getInstance(renderedRenderable), transformMatrix)
            }
        })
        animator.start()
    }

    override fun onResume() {
        super.onResume()
        choreographer.postFrameCallback(frameScheduler)
        animator.start()
    }

    override fun onPause() {
        super.onPause()
        choreographer.removeFrameCallback(frameScheduler)
        animator.cancel()
    }

    override fun onDestroy() {
        super.onDestroy()

        // Stop the animation and any pending frame
        choreographer.removeFrameCallback(frameScheduler)
        animator.cancel();

        // Always detach the surface before destroying the engine
        uiHelper.detach()

        // Cleanup all resources
        engine.destroyEntity(renderedRenderable)
        engine.destroyRenderer(renderer)
        engine.destroyVertexBuffer(renderedVertexBuffer)
        engine.destroyIndexBuffer(renderedIndexBuffer)
        engine.destroyMaterial(renderedMaterial)
        engine.destroyView(renderedView)
        engine.destroyScene(renderedScene)
        engine.destroyCamera(camera)

        // Engine.destroyEntity() destroys Filament related resources only
        // (components), not the entity itself
        val entityManager = EntityManager.get()
        entityManager.destroy(renderedRenderable)

        // Destroying the engine will free up any resource you may have forgotten
        // to destroy, but it's recommended to do the cleanup properly
        engine.destroy()
    }

    inner class FrameCallback : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            // Schedule the next frame
            choreographer.postFrameCallback(this)

            // This check guarantees that we have a swap chain
            if (uiHelper.isReadyToRender) {
                // If beginFrame() returns false you should skip the frame
                // This means you are sending frames too quickly to the GPU
                if (renderer.beginFrame(swapChain!!, frameTimeNanos)) {
                    renderer.render(renderedView)
                    renderer.render(postProcessView)
                    renderer.endFrame()
                }
            }
        }
    }

    inner class SurfaceCallback : UiHelper.RendererCallback {
        override fun onNativeWindowChanged(surface: Surface) {
            swapChain?.let { engine.destroySwapChain(it) }
            swapChain = engine.createSwapChain(surface, uiHelper.swapChainFlags)
            displayHelper.attach(renderer, surfaceView.display);


        }

        override fun onDetachedFromSurface() {
            displayHelper.detach();
            swapChain?.let {
                engine.destroySwapChain(it)
                // Required to ensure we don't return before Filament is done executing the
                // destroySwapChain command, otherwise Android might destroy the Surface
                // too early
                engine.flushAndWait()
                swapChain = null
            }
        }

        override fun onResized(width: Int, height: Int) {
            val zoom = 1.5
            texWidth = width
            texHeight = height
            val aspect = width.toDouble() / height.toDouble()
            camera.setProjection(Camera.Projection.ORTHO,
                    -aspect * zoom, aspect * zoom, -zoom, zoom, 0.0, 10.0)

            renderedView.viewport = Viewport(0, 0, width, height)

            renderedColorTex = Texture.Builder()
                    .width(width)
                    .height(height)
                    .sampler(Texture.Sampler.SAMPLER_2D)
                    .format(Texture.InternalFormat.RGBA8)
                    .usage(Texture.Usage.COLOR_ATTACHMENT)
                    .build(engine)

            renderedDepthTex = Texture.Builder()
                    .width(width)
                    .height(height)
                    .sampler(Texture.Sampler.SAMPLER_2D)
                    .format(Texture.InternalFormat.DEPTH24_STENCIL8)
                    .usage(Texture.Usage.DEPTH_ATTACHMENT)
                    .build(engine)

            renderedTarget = RenderTarget.Builder()
                    .texture(RenderTarget.AttachmentPoint.COLOR, renderedColorTex)
                    .texture(RenderTarget.AttachmentPoint.DEPTH, renderedDepthTex)
                    .build(engine)

//            postProcessColorTex = Texture.Builder()
//                    .width(width)
//                    .height(height)
//                    .sampler(Texture.Sampler.SAMPLER_2D)
//                    .format(Texture.InternalFormat.RGBA8)
//                    .usage(Texture.Usage.COLOR_ATTACHMENT)
//                    .build(engine)
//
//            postProcessTarget = RenderTarget.Builder()
//                    .texture(RenderTarget.AttachmentPoint.COLOR, postProcessColorTex)
//                    .build(engine)
            val sampler = TextureSampler(TextureSampler.MinFilter.LINEAR, TextureSampler.MagFilter.LINEAR, TextureSampler.WrapMode.REPEAT)
            postProcessMaterial.defaultInstance.setParameter("albedo", renderedColorTex, sampler)
            postProcessView.viewport = Viewport(0, 0, width, height)

            //do off-screen rendering first
            renderedView.renderTarget = renderedTarget
            //then do on-screen rendering
            postProcessView.renderTarget = null
        }
    }

    private fun readUncompressedAsset(assetName: String): ByteBuffer {
        assets.openFd(assetName).use { fd ->
            val input = fd.createInputStream()
            val dst = ByteBuffer.allocate(fd.length.toInt())

            val src = Channels.newChannel(input)
            src.read(dst)
            src.close()

            return dst.apply { rewind() }
        }
    }
}
