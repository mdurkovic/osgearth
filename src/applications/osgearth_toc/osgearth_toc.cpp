/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2008-2010 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/


#include <osgEarth/Export>
#include <osg/Version>
#include <osg/Notify>

#if OSG_MIN_VERSION_REQUIRED(2,8,0)

#include <osgUtil/Optimizer>
#include <osgDB/ReadFile>
#include <osgViewer/Viewer>

#include <osg/Material>
#include <osg/Geode>
#include <osg/BlendFunc>
#include <osg/Depth>
#include <osg/Projection>
#include <osg/AutoTransform>
#include <osg/Geometry>
#include <osg/Image>
#include <osg/CullFace>

#include <osgTerrain/TerrainTile>
#include <osgTerrain/GeometryTechnique>

#include <osgDB/WriteFile>

#include <osgText/Text>

#include <iostream>

#include <osgEarth/MapNode>
#include <osgEarth/FindNode>
#include <osgEarth/TileSource>
#include <osgEarth/Registry>

#include <osgEarthUtil/Common>
#include <osgEarthUtil/FadeLayerNode>
#include <osgEarthUtil/EarthManipulator>

#include <osgEarthDrivers/arcgis/ArcGISOptions>
#include <osgEarthDrivers/tms/TMSOptions>
#include <osgEarthDrivers/engine_osgterrain/OSGTerrainOptions>

#include <osgGA/StateSetManipulator>
#include <osgGA/TrackballManipulator>
#include <osgGA/FlightManipulator>
#include <osgGA/DriveManipulator>
#include <osgGA/KeySwitchMatrixManipulator>
#include <osgGA/StateSetManipulator>
#include <osgGA/AnimationPathManipulator>
#include <osgGA/TerrainManipulator>


#include <osgWidget/Util>
#include <osgWidget/WindowManager>
#include <osgWidget/Box>
#include <osgWidget/Label>
#include <osgWidget/ViewerEventHandlers>



#include <osgViewer/ViewerEventHandlers>

using namespace osg;
using namespace osgDB;
using namespace osgEarth;
using namespace osgEarthUtil;
using namespace osgEarth::Drivers;
using namespace osgWidget;

Vec4 normalColor(1,1,1,1);
Vec4 hotColor(0.4,0.56,1,1);
float textSize = 25.0f;
bool hudDirty = false;

const unsigned int MASK_2D = 0xF0000000;

struct BlankTileSource : public osgEarth::TileSource 
{
    BlankTileSource() { }

	virtual void initialize( const std::string& referenceURI, const Profile* overrideProfile =0)
	{
		if (overrideProfile)
		{
			setProfile( overrideProfile );
		}
		else
		{
			setProfile( osgEarth::Registry::instance()->getGlobalGeodeticProfile() );
		}
	}


    virtual osg::Image* createImage( const TileKey& key, ProgressCallback* progress ) {
        osg::Image* image = new osg::Image();
        image->setAllocationMode( osg::Image::USE_NEW_DELETE );
        image->allocateImage( 256, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE );
        for( int y=0; y<image->t(); y++ ) {
            for( int x=0; x<image->s(); x++ ) {
                *((unsigned int*)(image->data(x,y))) = 0xff00ff00;
            }
        }
        return image;
    }

};

//Simple hot tracking callback that changes the color of labels when the mouse enters and leaves
struct HotTrackingCallback: public osgWidget::Callback {
    HotTrackingCallback(const osg::Vec4 &normalColor, const osg::Vec4 &hotColor):
        osgWidget::Callback(osgWidget::EventType(osgWidget::EVENT_MOUSE_ENTER | osgWidget::EVENT_MOUSE_LEAVE)),
        _normalColor(normalColor),
        _hotColor(hotColor) { }

    virtual bool operator()(osgWidget::Event& ev) {
        if (ev.type == osgWidget::EVENT_MOUSE_ENTER)
        {
            ((osgWidget::Label*)ev.getWidget())->setFontColor(_hotColor.r(), _hotColor.g(), _hotColor.b(), _hotColor.a());
        }
        else if (ev.type == osgWidget::EVENT_MOUSE_LEAVE)
        {
            ((osgWidget::Label*)ev.getWidget())->setFontColor(_normalColor.r(), _normalColor.g(), _normalColor.b(), _normalColor.a());
        }
        return true;
    }

    osg::Vec4 _normalColor;
    osg::Vec4 _hotColor;
};

//Callback that toggles the visibility of a layer
struct ToggleVisiblityCallback: public osgWidget::Callback {
    ToggleVisiblityCallback(FadeLayerNode* fadeLayerNode, unsigned int layerIndex):
        osgWidget::Callback(osgWidget::EVENT_MOUSE_PUSH),
        _fadeLayerNode(fadeLayerNode),
        _layerIndex(layerIndex) { }

    virtual bool operator()(osgWidget::Event& ev) {
        if (ev.type == osgWidget::EVENT_MOUSE_PUSH)
        {
            MapFrame mapf(_fadeLayerNode->getMap());
			mapf.imageLayers()[_layerIndex]->setEnabled(
                mapf.imageLayers()[_layerIndex]->getEnabled() == false);
        }
        return true;
    }

    osg::ref_ptr<FadeLayerNode> _fadeLayerNode;
    unsigned int _layerIndex;
};

//Callback that increases/decreases the opacity of a layer
struct OpacityCallback: public osgWidget::Callback
{
    OpacityCallback(FadeLayerNode* fadeLayerNode, unsigned int layerIndex, float opacityDelta):
        osgWidget::Callback(osgWidget::EVENT_MOUSE_PUSH),
        _fadeLayerNode(fadeLayerNode),
        _layerIndex(layerIndex),
        _opacityDelta(opacityDelta) { }

    virtual bool operator()(osgWidget::Event& ev) {
        if (ev.type == osgWidget::EVENT_MOUSE_PUSH)
        {
            MapFrame mapf( _fadeLayerNode->getMap() );
            float oldOpacity = mapf.imageLayers()[_layerIndex]->getOpacity();
            mapf.imageLayers()[_layerIndex]->setOpacity( oldOpacity + _opacityDelta );
        }
        return true;
    }

    osg::ref_ptr<FadeLayerNode> _fadeLayerNode;
    unsigned int _layerIndex;
    float _opacityDelta;
};

//Callback that removes a Layer
struct RemoveLayerCallback: public osgWidget::Callback
{
    RemoveLayerCallback(Map* map, osgViewer::View* view, unsigned int layerIndex):
        osgWidget::Callback(osgWidget::EVENT_MOUSE_PUSH),
        _map( map ),
        _view(view),
        _layerIndex(layerIndex) { }

    virtual bool operator()(osgWidget::Event& ev) {
        if (ev.type == osgWidget::EVENT_MOUSE_PUSH)
        {
            //_view->getDatabasePager()->clear();
            MapFrame mapf( _map );
            _map->removeImageLayer( mapf.imageLayers()[_layerIndex] );
            //_mapNode->removeImageSource( _layerIndex );
            hudDirty = true;
        }
        return true;
    }

    osg::ref_ptr<Map> _map;
    unsigned int _layerIndex;
    osgViewer::View* _view;
};

//Callback that removes a Layer
struct MoveLayerCallback: public osgWidget::Callback
{
    MoveLayerCallback(Map* map, osgViewer::View* view, unsigned int layerIndex, bool up):
        osgWidget::Callback(osgWidget::EVENT_MOUSE_PUSH),
        _map(map),
        _view(view),
        _layerIndex(layerIndex),
        _up(up) { }

    virtual bool operator()(osgWidget::Event& ev) {
        if (ev.type == osgWidget::EVENT_MOUSE_PUSH)
        {
            MapFrame mapf(_map);
            //_view->getDatabasePager()->clear();
            int dir = _up ? 1 : -1;
            unsigned int newPosition = osg::clampBetween(_layerIndex + dir, 0u, (unsigned int)mapf.imageLayers().size()-1u);
            //_map->moveImageSource( _layerIndex, newPosition );
            ImageLayer* layer = mapf.imageLayers()[_layerIndex];
            _map->moveImageLayer( layer, newPosition );
            hudDirty = true;
        }
        return true;
    }

    osg::ref_ptr<Map> _map;
    osgViewer::View* _view;
    unsigned int _layerIndex;
    bool _up;
};

class AddLayerButton : public osgWidget::Label
{
public:
    AddLayerButton(Map* map, osgViewer::View* view, ImageLayer* layer) :
      osgWidget::Label("",""),
          _map(map),
          _view(view),
          _layer(layer)
      {
          setEventMask(osgWidget::EVENT_ALL);
          setFont("fonts/vera.ttf");
          setColor(1.0f, 1.0f, 1.0f, 0.0f);
          setFontSize(textSize);
          setFontColor(1.0f, 1.0f, 1.0f, 1.0f);
          setLabel(std::string("Add ") + layer->getName());
          addCallback(new HotTrackingCallback(normalColor, hotColor));
          getText()->setBackdropType(osgText::Text::OUTLINE);
      }

     
#if OSG_MIN_VERSION_REQUIRED(2,9,6)
      virtual bool mousePush(double, double, const osgWidget::WindowManager*) {
#else
      virtual bool mousePush(double, double, osgWidget::WindowManager*) {
#endif
         //_view->getDatabasePager()->clear();
         _map->addImageLayer( _layer );
         //_mapNode->addImageSource( _sourceConfig );
          hudDirty = true;
         return true;
     }

     osg::ref_ptr<Map> _map;
     osg::ref_ptr<ImageLayer> _layer;
     osgViewer::View* _view;
     osg::ref_ptr<FadeLayerNode> _fadeLayerNode;
};

void createAddLayersMenu(osgWidget::WindowManager* wm, FadeLayerNode* fadeLayerNode, Map* map, osgViewer::View* view)
{
    osgWidget::Box* addLayersBox = new osgWidget::Box("AddLayersBox", osgWidget::Box::VERTICAL);
    
    // Custom green layer:
    {
		BlankTileSource* tileSource = new BlankTileSource();
		tileSource->initialize( "" );
        ImageLayerOptions layerOpt;
        layerOpt.name() = "Green";
        ImageLayer* layer = new ImageLayer( layerOpt, tileSource );
        addLayersBox->addWidget( new AddLayerButton(map, view, layer) );
    }

    // ESRI reference labels
    {
        ArcGISOptions opt;
        opt.url() = "http://server.arcgisonline.com/ArcGIS/rest/services/Reference/ESRI_Boundaries_World_2D/MapServer";
        addLayersBox->addWidget( new AddLayerButton( map, view, new ImageLayer( "ESRI Boundaries", opt ) ) );
    }

    // ArcGIS transportation layer:
    {
        ArcGISOptions opt;
        opt.url() = "http://server.arcgisonline.com/ArcGIS/rest/services/Reference/ESRI_Transportation_World_2D/MapServer";
        addLayersBox->addWidget( new AddLayerButton( map, view, new ImageLayer( "ESRI Transportation", opt ) ) );
    }

    // OpenStreetMap:
    {
        TMSOptions driverOpt;
        driverOpt.url() = "http://tile.openstreetmap.org";
        driverOpt.format() = "png";
        driverOpt.tileSize() = 256;
        driverOpt.tmsType() = "google";

        ImageLayerOptions layerOpt( "OpenStreetMap", driverOpt );
        layerOpt.profile() = ProfileOptions( "global-mercator" );

        ImageLayer* layer = new ImageLayer( layerOpt );
        addLayersBox->addWidget( new AddLayerButton( map, view, layer ) );
    }

    // ArcGIS imagery:
    {
        ArcGISOptions opt;
        opt.url() = "http://server.arcgisonline.com/ArcGIS/rest/services/ESRI_Imagery_World_2D/MapServer";
        addLayersBox->addWidget( new AddLayerButton( map, view, new ImageLayer( "ESRI Imagery", opt ) ) );
    }

    addLayersBox->getBackground()->setColor(1,0,0,0.3);
    addLayersBox->setAnchorHorizontal(osgWidget::Window::HA_RIGHT);
    wm->addChild(addLayersBox);
    addLayersBox->resize();
}

class Line : public osgWidget::Box
{
public:
    Line(Map* map, FadeLayerNode* fadeLayerNode, osgViewer::View* view, unsigned int layerIndex):
        _map(map),
        _fadeLayerNode(fadeLayerNode),
        _layerIndex(layerIndex),
        _view(view),
        osgWidget::Box("", osgWidget::Box::HORIZONTAL)
    {
        //Create the number string
        _lblNum = createLabel();
        addWidget(_lblNum.get());

        //Create a button to remove the layer
        _lblRemove = createLabel();
        _lblRemove->setLabel("X");
        _lblRemove->addCallback(new RemoveLayerCallback(_map, view, _layerIndex));
        addWidget(_lblRemove.get());

        //Add a label to turn down the opacity
        _lblOpacityDown = createLabel();
        _lblOpacityDown->setLabel("<");        
        _lblOpacityDown->addCallback(new OpacityCallback(_fadeLayerNode, _layerIndex, -0.1));
        addWidget(_lblOpacityDown.get());

        //Add a label to turn the opacity up
        _lblOpacityUp = createLabel();
        _lblOpacityUp->setLabel(">");
        _lblOpacityUp->addCallback(new OpacityCallback(_fadeLayerNode, _layerIndex, 0.1));
        addWidget(_lblOpacityUp.get());

        //Create a button to move the layer up
        _lblMoveUp = createLabel();
        _lblMoveUp->setLabel("Up");
        _lblMoveUp->addCallback(new MoveLayerCallback(_map, view, _layerIndex, true));
        addWidget(_lblMoveUp.get());

        //Create a button to move the layer down
        _lblMoveDown = createLabel();
        _lblMoveDown->setLabel("Down");
        _lblMoveDown->addCallback(new MoveLayerCallback(_map, view, _layerIndex, false));
        addWidget(_lblMoveDown.get());

        //Add a label with the name of the layer, clicking on the layer 
        _lblName = createLabel();
        _lblName->addCallback(new ToggleVisiblityCallback(fadeLayerNode, _layerIndex));
        addWidget(_lblName.get());

        getBackground()->setColor(0,0,0,0);

        updateText();
    }

    void updateText()
    {
        MapFrame mapf( _map );
        if ( _layerIndex < mapf.imageLayers().size() )
        {
            std::string name = mapf.imageLayers()[_layerIndex]->getName(); //Node->getImageSource( _layerIndex );
            std::stringstream ss;
            unsigned int index = (mapf.imageLayers().size() - _layerIndex);
            ss << index << ") ";
			std::string ssStr;
			ssStr = ss.str();
            _lblNum->setLabel(ssStr);
            _lblName->setLabel(name);
        }
    }

    osgWidget::Label* createLabel()
    {
        osgWidget::Label* label = new osgWidget::Label("", "");
        label->setFont("fonts/vera.ttf");
        label->setColor(1.0f, 1.0f, 1.0f, 0.0f);
        label->setFontSize(textSize);
        label->setFontColor(1.0f, 1.0f, 1.0f, 1.0f);
        label->getText()->setBackdropType(osgText::Text::OUTLINE);
        label->setEventMask(osgWidget::EVENT_ALL);  
        label->setPadding(3.0f);
        label->addCallback(new HotTrackingCallback(normalColor, hotColor));
        return label;
    }

    osg::ref_ptr<osgWidget::Label> _lblName;
    osg::ref_ptr<osgWidget::Label> _lblNum;
    osg::ref_ptr<osgWidget::Label> _lblMoveUp;
    osg::ref_ptr<osgWidget::Label> _lblMoveDown;
    osg::ref_ptr<osgWidget::Label> _lblOpacityUp;
    osg::ref_ptr<osgWidget::Label> _lblOpacityDown;
    osg::ref_ptr<osgWidget::Label> _lblRemove;
    unsigned int _layerIndex;
    osg::ref_ptr<Map> _map;
    osg::ref_ptr<FadeLayerNode> _fadeLayerNode;
    osgViewer::View* _view;
};

class TOC
{
public:
    TOC(osgWidget::WindowManager* wm, Map* map, FadeLayerNode* fadeLayerNode, osgViewer::View* view):
      _wm(wm),
      _map(map),
      _fadeLayerNode(fadeLayerNode),
      _view(view)
    {
          //Create the lines
          unsigned int maxLayers = 4;
          for (unsigned int i = 0; i < maxLayers; ++i)
          {
              Line* line = new Line(_map, fadeLayerNode, _view, i);
              _lines.push_back(line);
          }
          update();
    }

      void update()
      {
          MapFrame mapf(_map);

          //Remove the existing lines
          for (unsigned int i = 0; i < _lines.size(); ++i)
          {
              _wm->removeChild(_lines[i].get());
          }

          for (unsigned int i = 0; i < mapf.imageLayers().size(); ++i)
          {
              Line* line = i < _lines.size() ? _lines[i] : NULL;
              if (line)
              {
                  line->updateText();
                  //Push the row up
                  line->setOrigin(0, (textSize + 10)*i);
                  _wm->addChild(line);
              }
          }
      }

      std::vector<osg::ref_ptr<Line> > _lines;

      osgWidget::WindowManager* _wm;
      Map* _map;
      FadeLayerNode* _fadeLayerNode;
      osgViewer::View* _view;
};

class TOCUpdateCallback : public osg::NodeCallback
{
public:
    TOCUpdateCallback(TOC* toc)
    {
        _toc = toc;
    }

    virtual void operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        if (hudDirty)
        {
            _toc->update();
            hudDirty = false;
        }
        traverse(node, nv);
    }

    TOC* _toc;
};

int main(int argc, char** argv)
{
    osg::ArgumentParser arguments(&argc,argv);

    OSGTerrainOptions terrainOptions;

    terrainOptions.loadingPolicy()->mode() = LoadingPolicy::MODE_SEQUENTIAL;

    if ( arguments.read( "--preemptive" ) || arguments.read( "--preemptive=ON" ) )
    {
        terrainOptions.loadingPolicy()->mode() = LoadingPolicy::MODE_PREEMPTIVE;
    }
    else if ( arguments.read( "--standard" ) || arguments.read( "--standard=ON" ) )
    {
        terrainOptions.loadingPolicy()->mode() = LoadingPolicy::MODE_STANDARD;
    }
    else if ( arguments.read( "--sequential" ) || arguments.read( "--sequential=ON" ) )
    {
        terrainOptions.loadingPolicy()->mode() = LoadingPolicy::MODE_SEQUENTIAL;
    }

	if (arguments.read( "--multipass") )
	{
        terrainOptions.compositingTechnique() = TerrainOptions::COMPOSITING_MULTIPASS;
        //Multipass mode is currently only available in STANDARD mode.
        terrainOptions.loadingPolicy()->mode() = LoadingPolicy::MODE_STANDARD;
	}

    // construct the viewer.
    osgViewer::Viewer viewer(arguments);

	{
        osg::ref_ptr<osgGA::KeySwitchMatrixManipulator> keyswitchManipulator = new osgGA::KeySwitchMatrixManipulator;

        keyswitchManipulator->addMatrixManipulator( '1', "EarthManipulator", new osgEarthUtil::EarthManipulator() );
        keyswitchManipulator->addMatrixManipulator( '2', "Flight", new osgGA::FlightManipulator() );
        keyswitchManipulator->addMatrixManipulator( '3', "Drive", new osgGA::DriveManipulator() );
        keyswitchManipulator->addMatrixManipulator( '4', "Terrain", new osgGA::TerrainManipulator() );

        std::string pathfile;
        char keyForAnimationPath = '6';
        while (arguments.read("-p",pathfile))
        {
            osgGA::AnimationPathManipulator* apm = new osgGA::AnimationPathManipulator(pathfile);
            if (apm || !apm->valid()) 
            {
                unsigned int num = keyswitchManipulator->getNumMatrixManipulators();
                keyswitchManipulator->addMatrixManipulator( keyForAnimationPath, "Path", apm );
                keyswitchManipulator->selectMatrixManipulator(num);
                ++keyForAnimationPath;
            }
        }

        viewer.setCameraManipulator( keyswitchManipulator.get() );
    }


    osg::Group* group = new osg::Group;


    MapOptions mapOptions;
    mapOptions.coordSysType() = MapOptions::CSTYPE_GEOCENTRIC;
    if (arguments.read("--cube"))
    {
        mapOptions.coordSysType() = MapOptions::CSTYPE_GEOCENTRIC_CUBE;
    }

    Map* map = new Map(mapOptions);

    MapNodeOptions mapNodeOptions( terrainOptions );
    MapNode* mapNode = new MapNode(map, mapNodeOptions);
    osg::ref_ptr<osg::Node> loadedModel = mapNode;

    FadeLayerNode* fadeLayerNode = new FadeLayerNode( mapNode->getMap(), mapNodeOptions );
    fadeLayerNode->addChild(loadedModel.get());
    group->addChild(fadeLayerNode);
	//group->addChild( loadedModel.get() );

    MapFrame mapf( mapNode->getMap() );

    for (unsigned int i = 0; i < mapf.imageLayers().size(); ++i)
    {
		mapf.imageLayers()[i]->setOpacity( 1.0f ); //opacity() = 1.0f;
		mapf.imageLayers()[i]->setEnabled(true);
    }

    //Setup the osgWidget interface
    osgWidget::WindowManager* wm = new osgWidget::WindowManager(
        &viewer,
        800.0f,
        800.0f,
        MASK_2D,
        0 );

    TOC toc(wm, mapNode->getMap(), fadeLayerNode, &viewer);
    createAddLayersMenu(wm, fadeLayerNode, mapNode->getMap(), &viewer);
    group->setDataVariance(osg::Object::DYNAMIC);
    group->setUpdateCallback(new TOCUpdateCallback(&toc));


    viewer.setUpViewInWindow(10, 10, 800,800);

    osg::Camera* camera = wm->createParentOrthoCamera();
    group->addChild(camera);

    viewer.addEventHandler(new osgWidget::MouseHandler(wm));
    viewer.addEventHandler(new osgWidget::KeyboardHandler(wm));
    viewer.addEventHandler(new osgWidget::ResizeHandler(wm, camera));
    viewer.addEventHandler(new osgWidget::CameraSwitchHandler(wm, camera));

    viewer.addEventHandler(new osgViewer::StatsHandler());
    viewer.addEventHandler(new osgViewer::WindowSizeHandler());
    // add the state manipulator
    viewer.addEventHandler( new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()) );


    wm->resizeAllWindows();


    // set the scene to render
    viewer.setSceneData(group);

    // run the viewers frame loop
    return viewer.run();
}

#else

int main(int argc, char** argv)
{
    OE_NOTICE << "osgearth_toc requires at least OpenSceneGraph 2.8.0 due to changes in osgWidget." << std::endl;
    return 0;
}

#endif