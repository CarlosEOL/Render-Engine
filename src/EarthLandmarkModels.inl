// Procedural landmark geometry. Lengths are metres in the site's east/up/south frame.
const Paint ivory(.79f,.76f,.65f,0), charcoal(.23f,.26f,.27f,0);
const Paint roofTile(.45f,.16f,.09f,0), glazedTile(.10f,.26f,.28f,0);
const Paint copperPatina(.28f,.56f,.43f,0), windowGlass(.11f,.24f,.31f,.02f);

void LandmarkBeam(Builder& mesh,V start,V end,float width,Paint paint) {
    const V along=glm::normalize(end-start);
    const V reference=std::abs(along.y)>.95f?V(1,0,0):V(0,1,0);
    const V across=glm::normalize(glm::cross(along,reference))*width*.5f;
    const V up=glm::normalize(glm::cross(across,along))*width*.5f;
    const V corners[]={across+up,-across+up,-across-up,across-up};
    for(int i=0;i<4;++i){
        const V a=corners[i],b=corners[(i+1)%4];
        mesh.Quad(start+a,start+b,end+b,end+a,paint,a+b);
    }
    mesh.Quad(start+corners[0],start+corners[1],start+corners[2],start+corners[3],paint,-along);
    mesh.Quad(end+corners[0],end+corners[1],end+corners[2],end+corners[3],paint,along);
}

void LandmarkEllipsoid(Builder& mesh,V centre,V radii,Paint paint,bool dome=false) {
    constexpr int sides=24,rings=10;
    const float latitudeStart=dome?0.f:-glm::half_pi<float>();
    const auto point=[&](int row,int column){
        const float latitude=glm::mix(latitudeStart,glm::half_pi<float>(),float(row)/rings);
        const float longitude=glm::two_pi<float>()*column/sides;
        return centre+radii*V(std::cos(latitude)*std::cos(longitude),
            std::sin(latitude),std::cos(latitude)*std::sin(longitude));
    };
    for(int i=0;i<rings;++i)for(int j=0;j<sides;++j){
        const V a=point(i,j),b=point(i,j+1),c=point(i+1,j+1),d=point(i+1,j);
        mesh.Quad(a,b,c,d,paint,(a+b+c+d)*.25f-centre);
    }
}

void LandmarkRoof(Builder& mesh,V base,float width,float height,float depth,Paint paint) {
    const V front(-width*.5f,0,depth*.5f),back(-width*.5f,0,-depth*.5f);
    const V across(width,0,0),ridge(width*.5f,height,0);
    mesh.Quad(base+front,base+back,base+back+ridge,base+front+ridge,paint,{-1,1,0});
    mesh.Quad(base+front+ridge,base+back+ridge,base+back+across,base+front+across,paint,{1,1,0});
    mesh.Triangle(base+front,base+front+across,base+front+ridge,paint,{0,0,1});
    mesh.Triangle(base+back,base+back+across,base+back+ridge,paint,{0,0,-1});
}

void LandmarkWindows(Builder& mesh,V base,float width,float height,float depth,int columns,int stories) {
    const float stepX=width/columns,stepY=height/stories;
    for(int row=0;row<stories;++row)for(int column=0;column<columns;++column){
        const float x=(column+.5f)*stepX-width*.5f;
        for(int side:{-1,1}){
            const V entry=base+V(x,(row+.17f)*stepY,side*(depth*.5f+.035f));
            mesh.Box(entry,{stepX*.47f,stepY*.56f,.075f},windowGlass);
        }
    }
}

void LandmarkSpire(Builder& mesh,V top,float radius,float height,Paint body,Paint cap) {
    mesh.Round(top,radius,radius*.87f,height*.68f,body,16);
    mesh.Round(top+V(0,height*.68f,0),radius*.92f,.02f,height*.30f,cap,16);
    mesh.Round(top+V(0,height*.98f,0),radius*.08f,.025f,height*.10f,bronze,8);
}

void LandmarkEiffel(Builder& mesh,float width,float height,float depth) {
    const Paint iron(.44f,.31f,.21f,0),ironShade(.31f,.23f,.18f,0);
    constexpr float level[]={0,.10f,.20f,.29f,.39f,.51f,.63f,.75f,.86f,.94f};
    const auto spread=[](float y){return .475f*std::pow(1.f-y,2.45f)+.010f*y;};
    const auto leg=[&](int x,int z,float y){return V(width*x*spread(y),height*y,depth*z*spread(y));};
    const float unit=std::min(width,depth);
    for(int x:{-1,1})for(int z:{-1,1}){
        const V foot=leg(x,z,0);
        mesh.Box(foot-V(0,3,0),{unit*.115f,4,unit*.115f},limestone);
        for(unsigned i=1;i<std::size(level);++i){
            const float y0=level[i-1],y1=level[i];
            const V low=leg(x,z,y0),high=leg(x,z,y1);
            LandmarkBeam(mesh,low,high,unit*glm::mix(.037f,.010f,y1),iron);
            const float lowInset=unit*glm::mix(.052f,.009f,y0);
            const float highInset=unit*glm::mix(.052f,.009f,y1);
            for(const V inward:{V(-x,0,0),V(0,0,-z)}){
                LandmarkBeam(mesh,low+inward*lowInset,high+inward*highInset,unit*.012f,ironShade);
                LandmarkBeam(mesh,low+inward*lowInset,high,unit*.0065f,iron);
                LandmarkBeam(mesh,low,high+inward*highInset,unit*.0065f,iron);
            }
        }
    }
    for(float y:{.19f,.35f,.84f,.935f}){
        const float span=spread(y)*2.f+.09f*(1-y);
        mesh.Box({0,height*y,0},{width*span,height*.018f,depth*span},ironShade);
    }
    for(int side:{-1,1})for(bool front:{false,true}){
        const float span=(front?width:depth)*.33f;
        const float offset=(front?depth:width)*.33f*side;
        const auto point=[&](float angle){
            const float x=std::cos(angle)*span,y=height*(.035f+.14f*std::sin(angle));
            return front?V(x,y,offset):V(offset,y,x);
        };
        for(int i=0;i<12;++i)
            LandmarkBeam(mesh,point(i*glm::pi<float>()/12),point((i+1)*glm::pi<float>()/12),unit*.017f,iron);
    }
    mesh.Round({0,height*.945f,0},unit*.015f,unit*.004f,height*.055f,iron,12);
}

void LandmarkLiberty(Builder& mesh,float width,float height,float depth) {
    const auto point=[&](float x,float y,float z){return V(width*x,height*y,depth*z);};
    const float baseWidth=std::min(width,depth);
    mesh.Box({0,-2,0},{width,2+height*.11f,depth},sandstone);
    mesh.SquareStage({0,height*.11f,0},baseWidth*.76f,baseWidth*.59f,height*.09f,paleStone);
    mesh.Box({0,height*.20f,0},{baseWidth*.54f,height*.265f,baseWidth*.54f},limestone);
    mesh.Box({0,height*.465f,0},{baseWidth*.65f,height*.025f,baseWidth*.65f},paleStone);
    mesh.Round({0,height*.49f,0},baseWidth*.25f,baseWidth*.13f,height*.20f,copperPatina,16);
    for(int i=0;i<16;++i){
        const float theta=glm::two_pi<float>()*i/16;
        const V direction(std::cos(theta),0,std::sin(theta));
        LandmarkBeam(mesh,{0,height*.675f,0},direction*(baseWidth*.24f)+V(0,height*.498f,0),
            baseWidth*.035f,glm::mix(copperPatina,bathStone,.43f));
    }
    LandmarkEllipsoid(mesh,point(0,.708f,0),point(.145f,.075f,.13f),copperPatina);
    LandmarkEllipsoid(mesh,point(.005f,.802f,.035f),point(.063f,.050f,.065f),copperPatina);
    for(int i=0;i<7;++i){
        const float angle=glm::pi<float>()*i/6;
        LandmarkBeam(mesh,point(std::cos(angle)*.05f,.817f,std::sin(angle)*.058f),
            point(std::cos(angle)*.134f,.874f,std::sin(angle)*.12f),baseWidth*.019f,copperPatina);
    }
    LandmarkBeam(mesh,point(-.09f,.733f,0),point(-.21f,.843f,0),baseWidth*.088f,copperPatina);
    LandmarkBeam(mesh,point(-.21f,.843f,0),point(-.245f,.925f,.015f),baseWidth*.065f,copperPatina);
    mesh.Round(point(-.245f,.918f,.015f),baseWidth*.03f,baseWidth*.045f,height*.032f,copperPatina,12);
    LandmarkEllipsoid(mesh,point(-.245f,.972f,.015f),point(.038f,.028f,.038f),bronze);
    LandmarkBeam(mesh,point(.09f,.729f,0),point(.15f,.646f,.12f),baseWidth*.078f,copperPatina);
    mesh.Box(point(.105f,.646f,.14f),point(.134f,.113f,.035f),copperPatina);
}

void LandmarkOpera(Builder& mesh,float width,float height,float depth) {
    mesh.Box({0,-1,0},{width,height*.13f+1,depth},sandstone);
    const auto sail=[&](V base,float span,float rise,float length,float tilt){
        const auto point=[&](float t,float u){
            const float flank=std::sqrt(std::max(0.f,1.f-t*t));
            const float sweep=t*t;
            return base+V(span*u*flank+tilt*sweep,rise*std::sin(t*glm::half_pi<float>())*(1.f-.26f*u*u*flank),
                length*(t-.54f)+length*.26f*(1-u*u)*flank);
        };
        for(int i=0;i<14;++i)for(int j=0;j<12;++j){
            const float t0=float(i)/14,t1=float(i+1)/14,u0=float(j)/6-1,u1=float(j+1)/6-1;
            const V a=point(t0,u0),b=point(t0,u1),c=point(t1,u1),d=point(t1,u0);
            const Paint tile=(i+j)%4?ivory:marble;
            if(i==13)mesh.Triangle(a,b,c,tile,{0,1,0});
            else mesh.Quad(a,b,c,d,tile,{0,1,0});
            if(j==0 || j==11){
                const V low=j?b:a,high=j?c:d;
                const V lowerLow(low.x*.90f+base.x*.10f,glm::mix(base.y,low.y,.40f),low.z);
                const V lowerHigh(high.x*.90f+base.x*.10f,glm::mix(base.y,high.y,.40f),high.z);
                const V outward(j?1.f:-1.f,0,0);
                mesh.Quad(lowerLow,lowerHigh,high,low,tile,outward);
                mesh.Quad(V(lowerLow.x,base.y,low.z),V(lowerHigh.x,base.y,high.z),lowerHigh,lowerLow,windowGlass,outward);
            }
        }
    };
    for(int hall:{-1,1})for(int i=0;i<4;++i){
        const float scale=(1.f-i*.16f)*(hall<0?.78f:1.f);
        sail({width*.21f*hall,height*.13f,depth*(.23f-i*.16f)},width*.21f*scale,
            height*.86f*scale,depth*.48f*scale,width*.14f*hall*scale);
    }
    sail({width*.32f,height*.13f,depth*.32f},width*.13f,height*.39f,depth*.29f,-width*.045f);
    for(int i=0;i<8;++i)
        mesh.Box({0,height*.008f*i,-depth*(.50f-i*.008f)},{width*.83f,height*.008f,depth*.017f},ivory);
}

void LandmarkAtomium(Builder& mesh,float width,float height,float depth) {
    std::array<V,9> vertices{};
    unsigned index=0;
    for(int x:{-1,1})for(int y:{-1,1})for(int z:{-1,1}){
        // Body diagonal vertical. This projects one sphere directly above the central sphere.
        const auto origin=V(float(x),float(y),float(z));
        const V horizontal=glm::normalize(V(1,0,-1)),vertical=glm::normalize(V(1,1,1));
        const V transverse=glm::cross(horizontal,vertical);
        vertices[index++]={glm::dot(origin,horizontal)*width*.26f,
            height*(.50f+glm::dot(origin,vertical)*.209f),glm::dot(origin,transverse)*depth*.26f};
    }
    vertices[8]={0,height*.5f,0};
    const Paint metal(.70f,.75f,.77f,0);
    const float unit=std::min({width,height,depth});
    for(unsigned i=0;i<vertices.size();++i){
        LandmarkEllipsoid(mesh,vertices[i],V(unit*.12f),metal);
        for(unsigned j=0;j<i;++j){
            const unsigned difference=i^j;
            if(i==8 || difference==1 || difference==2 || difference==4)
                LandmarkBeam(mesh,vertices[i],vertices[j],unit*.045f,oldStone);
        }
    }
    for(int i:{1,2,4}){
        const V p=vertices[i];
        LandmarkBeam(mesh,V(p.x*1.4f,0,p.z*1.4f),p,unit*.058f,oldStone);
    }
}

void LandmarkChrist(Builder& mesh,float width,float height,float depth) {
    const auto p=[&](float x,float y,float z){return V(width*x,height*y,depth*z);};
    const float radius=std::min(width,depth);
    mesh.Box({0,-2,0},{radius*.88f,height*.23f+2,depth},oldStone);
    mesh.Box(p(0,.23f,0),p(.33f,.027f,.79f),ivory);
    mesh.Round(p(0,.257f,0),radius*.32f,radius*.185f,height*.42f,marble,18);
    LandmarkEllipsoid(mesh,p(0,.71f,0),p(.135f,.15f,.28f),ivory);
    LandmarkEllipsoid(mesh,p(0,.91f,.018f),p(.068f,.082f,.13f),marble);
    for(int side:{-1,1}){
        LandmarkBeam(mesh,p(.07f*side,.768f,0),p(.43f*side,.776f,0),width*.098f,ivory);
        LandmarkEllipsoid(mesh,p(.448f*side,.774f,0),p(.052f,.048f,.113f),marble);
        LandmarkBeam(mesh,p(.07f*side,.657f,0),p(.345f*side,.745f,0),width*.044f,marble);
    }
    for(int fold=-2;fold<=2;++fold)
        LandmarkBeam(mesh,p(.055f*fold,.273f,.23f),p(.035f*fold,.654f,.19f),width*.018f,ivory);
}

void LandmarkSpires(Builder& mesh,float width,float height,float depth,bool onion) {
    const Paint facade=onion?roofTile:ivory;
    const float unit=std::min(width,depth);
    mesh.Box({0,-1,0},{width*.78f,height*.25f+1,depth*.73f},facade);
    if(onion){
        const Paint cap[]={glazedTile,roofTile,copperPatina,bronze};
        for(int i=0;i<9;++i){
            const float angle=glm::two_pi<float>()*i/8;
            const float x=i==8?0:std::cos(angle)*width*.33f,z=i==8?0:std::sin(angle)*depth*.33f;
            const float tower=i==8?.65f:.36f+.06f*(i%3);
            const float r=unit*(i==8?.13f:.11f);
            mesh.Round({x,height*.13f,z},r*.92f,r*.84f,height*tower,facade,8);
            mesh.Round({x,height*(.13f+tower),z},r*.84f,r,height*.035f,marble,12);
            LandmarkEllipsoid(mesh,{x,height*(.165f+tower),z},V(r,height*.16f,r),cap[i%4]);
            mesh.Round({x,height*(.29f+tower),z},r*.21f,r*.018f,height*.06f,bronze,8);
        }
    }else{
        for(int side:{-1,1})for(int i=0;i<4;++i){
            const float y=height*(.51f+i*.089f);
            const float x=width*side*(.16f+.067f*i),z=-depth*.34f+depth*.20f*i;
            const float r=unit*.046f;
            mesh.Round({x,0,z},r*1.37f,r*.86f,y,facade,12);
            for(int belt=1;belt<=5;++belt)
                mesh.Round({x,y*(.42f+.1f*belt),z},r,r*.90f,y*.023f,paleStone,12);
            mesh.Round({x,y,z},r*.88f,r*.02f,height*.096f,limestone,12);
            LandmarkEllipsoid(mesh,{x,y+height*.10f,z},V(r*.51f),glazedTile);
        }
        mesh.Round({0,height*.25f,0},unit*.12f,unit*.027f,height*.72f,ivory,18);
        mesh.Round({0,height*.97f,0},unit*.04f,0,height*.03f,marble,12);
        for(int i=-2;i<=2;++i)
            mesh.Arch({width*.13f*i,0,depth*.37f},{1,0,0},width*.037f,height*.13f,width*.018f,depth*.035f,limestone);
    }
}

void LandmarkFortress(Builder& mesh,float width,float height,float depth,bool ruined) {
    const float wallHeight=height*(ruined?.32f:.53f),thickness=std::min(width,depth)*.072f;
    for(int side:{-1,1}){
        for(bool front:{false,true}){
            const float length=front?width:depth;
            const V centre=front?V(0,0,side*depth*.41f):V(side*width*.41f,0,0);
            const V along=front?V(1,0,0):V(0,0,1);
            const float block=length/13;
            for(int i=-6;i<=6;++i){
                if((ruined && (i+side+14)%7==0) || (front && side==1 && i==0))continue;
                const V position=centre+along*(i*block);
                const float footing=mesh.Ground(position.x,position.z);
                const float top=wallHeight*(ruined?.60f+.14f*((i+8)%4):1.f);
                mesh.Box(position+V(0,footing-1.5f,0),{block+.012f,top+1.5f,thickness},oldStone,along);
                if(!ruined)mesh.Box(position+V(0,footing+top,0),{block*.55f,height*.10f,thickness*1.06f},ivory,along);
            }
        }
        for(int z:{-1,1}){
            const V foot(side*width*.40f,0,z*depth*.40f);
            const float y=mesh.Ground(foot.x,foot.z);
            mesh.Round(foot+V(0,y-2,0),thickness*1.48f,thickness*1.28f,height*(ruined?.54f:.77f)+2,limestone,16);
            if(!ruined)mesh.Round(foot+V(0,y+height*.77f,0),thickness*1.44f,.04f,height*.23f,roofTile,16);
        }
    }
}

void LandmarkBridge(Builder& mesh,float width,float height,float depth) {
    const float deck=height*.27f;
    mesh.Box({0,deck,0},{width,height*.05f,depth},charcoal);
    for(int z:{-1,1}){
        LandmarkBeam(mesh,{-width*.5f,deck+height*.073f,z*depth*.46f},
            {width*.5f,deck+height*.073f,z*depth*.46f},depth*.035f,oldStone);
        for(int pier:{-1,1}){
            const float x=width*.24f*pier;
            mesh.Box({x,-3,z*depth*.39f},{width*.043f,height+3,depth*.15f},oldStone);
            const V pylon(x,height,z*depth*.39f);
            const auto cable=[&](float t){
                return glm::mix(pylon,V(x-pier*width*.24f,deck+height*.06f,z*depth*.39f),t)
                    -V(0,.28f*height*t*(1-t),0);
            };
            LandmarkBeam(mesh,pylon,{pier*width*.49f,deck+height*.05f,z*depth*.39f},depth*.029f,charcoal);
            for(int i=0;i<10;++i){
                const V p=cable(float(i)/10),q=cable(float(i+1)/10);
                LandmarkBeam(mesh,p,q,depth*.029f,charcoal);
                LandmarkBeam(mesh,p,{p.x,deck+height*.04f,p.z},depth*.015f,charcoal);
            }
        }
    }
    for(int pier:{-1,1})
        mesh.Box({width*.24f*pier,height*.87f,0},{width*.045f,height*.075f,depth*.91f},oldStone);
}

void BuildLandmark(Builder& mesh,LandmarkForm form,float width,float height,float depth) {
    const auto p=[&](float x,float y,float z){return V(width*x,height*y,depth*z);};
    const float span=std::min(width,depth),footing=std::min(3.f,height*.13f);
    switch(form){
    case LandmarkForm::Eiffel: LandmarkEiffel(mesh,width,height,depth); break;
    case LandmarkForm::Liberty: LandmarkLiberty(mesh,width,height,depth); break;
    case LandmarkForm::Opera: LandmarkOpera(mesh,width,height,depth); break;
    case LandmarkForm::Atomium: LandmarkAtomium(mesh,width,height,depth); break;
    case LandmarkForm::Christ: LandmarkChrist(mesh,width,height,depth); break;
    case LandmarkForm::Sagrada: LandmarkSpires(mesh,width,height,depth,false); break;
    case LandmarkForm::StBasils: LandmarkSpires(mesh,width,height,depth,true); break;
    case LandmarkForm::Fortress: LandmarkFortress(mesh,width,height,depth,false); break;
    case LandmarkForm::Ruin: LandmarkFortress(mesh,width,height,depth,true); break;
    case LandmarkForm::Bridge: LandmarkBridge(mesh,width,height,depth); break;
    case LandmarkForm::Petronas: {
        const float radius=std::min(width*.173f,depth*.46f);
        for(int side:{-1,1}){
            const float x=width*.29f*side;
            mesh.Round({x,-footing,0},radius,radius,height*.42f+footing,windowGlass,24);
            for(int stage=0;stage<10;++stage){
                const float y=height*(.42f+stage*.046f);
                const float r=radius*(1-stage*.088f);
                mesh.Round({x,y,0},r,r*.99f,height*.046f,windowGlass,24);
            }
            for(int story=1;story<70;++story){
                const float progress=story/70.f*.88f;
                const float fac=1.f-.088f*std::max(0.f,std::floor((progress-.42f)/.046f));
                mesh.Round({x,height*progress,0},radius*fac*1.019f,radius*fac*1.019f,height*.004f,ivory,24);
            }
            mesh.Round({x,height*.88f,0},radius*.21f,radius*.012f,height*.12f,ivory,12);
        }
        mesh.Box({0,height*.43f,0},p(.57f,.025f,.41f),ivory);
        for(int side:{-1,1})
            LandmarkBeam(mesh,p(.21f*side,.36f,0),p(.075f*side,.43f,0),radius*.10f,ivory);
        break;
    }
    case LandmarkForm::BurjKhalifa: {
        const float core=width*.144f;
        mesh.Round({0,-footing,0},core,core,height*.75f+footing,windowGlass,24);
        for(int wing=0;wing<3;++wing){
            const float azimuth=glm::two_pi<float>()*wing/3;
            const V axis(std::cos(azimuth),0,std::sin(azimuth));
            for(int tier=0;tier<6;++tier){
                const float y=height*(tier*.105f+wing*.029f*(tier>0));
                const float next=tier==5?height*(.69f+wing*.025f):height*((tier+1)*.105f+wing*.029f);
                const float wingLength=width*(.54f-tier*.067f);
                const float wingWidth=width*(.235f-tier*.025f);
                const V center=axis*(wingLength*.41f)+V(0,y-(tier==0?footing:0),0);
                mesh.Box(center,{wingLength,next-center.y,wingWidth},windowGlass,axis);
                for(int floor=1;floor<13;++floor){
                    const float level=glm::mix(y,next,floor/13.f);
                    mesh.Box({center.x,level,center.z},{wingLength*1.012f,height*.0013f,wingWidth*1.04f},
                        oldStone,axis);
                }
            }
        }
        mesh.Round(p(0,.75f,0),core*.72f,core*.46f,height*.09f,windowGlass,24);
        mesh.Round(p(0,.84f,0),core*.46f,core*.22f,height*.071f,ivory,18);
        mesh.Round(p(0,.911f,0),core*.22f,core*.033f,height*.089f,ivory,12);
        break;
    }
    case LandmarkForm::BigBen: {
        mesh.Box({0,-footing,0},{width,height*.69f+footing,depth},limestone);
        for(int x:{-1,1})for(int z:{-1,1})
            mesh.Box(p(x*.43f,0,z*.43f),p(.16f,.831f,.16f),paleStone);
        LandmarkWindows(mesh,{0,0,0},width,height*.66f,depth,3,8);
        mesh.Box(p(0,.66f,0),p(1.08f,.023f,1.08f),paleStone);
        mesh.Box(p(0,.683f,0),p(.97f,.14f,.97f),ivory);
        for(const V face:{V(1,0,0),V(-1,0,0),V(0,0,1),V(0,0,-1)}){
            const V right=glm::cross(V(0,1,0),face);
            const V center=face*(span*.497f)+V(0,height*.73f,0);
            const float dial=span*.302f;
            for(int tick=0;tick<32;++tick){
                const float a=glm::two_pi<float>()*tick/32,b=glm::two_pi<float>()*(tick+1)/32;
                const V h=right*std::cos(a)+V(0,std::sin(a),0),j=right*std::cos(b)+V(0,std::sin(b),0);
                mesh.Triangle(center,center+h*dial,center+j*dial,marble,face);
                mesh.Quad(center+h*dial,center+j*dial,center+j*dial*1.09f,center+h*dial*1.09f,bronze,face);
            }
            const V hands=center+face*.03f;
            LandmarkBeam(mesh,hands,hands+right*dial*.69f+V(0,-dial*.37f,0),dial*.074f,charcoal);
            LandmarkBeam(mesh,hands,hands+right*dial*.20f+V(0,dial*.86f,0),dial*.050f,charcoal);
        }
        mesh.SquareStage(p(0,.823f,0),span*1.03f,span*.72f,height*.028f,charcoal);
        mesh.SquareStage(p(0,.851f,0),span*.72f,span*.29f,height*.109f,charcoal);
        mesh.SquareStage(p(0,.960f,0),span*.29f,span*.058f,height*.033f,bronze);
        mesh.Round(p(0,.993f,0),span*.037f,span*.006f,height*.007f,bronze,8);
        break;
    }
    case LandmarkForm::LeaningTower: {
        const auto first=mesh.vertices.size();
        mesh.Round({0,-footing,0},span*.50f,span*.47f,height*.18f+footing,marble,48);
        for(int course=0;course<7;++course){
            const float base=height*(.18f+course*.106f);
            const float outer=span*(course==6?.36f:.46f);
            mesh.Round({0,base,0},outer*.72f,outer*.72f,height*.106f,paleStone,48);
            mesh.Round({0,base,0},outer,outer,height*.014f,marble,48);
            const int columns=course==6?12:24;
            for(int column=0;column<columns;++column){
                const float angle=glm::two_pi<float>()*column/columns;
                const V edge(std::cos(angle)*outer*.90f,0,std::sin(angle)*outer*.90f);
                mesh.Round(edge+V(0,base,0),span*.022f,span*.020f,height*.099f,marble,10);
            }
            mesh.Round({0,base+height*.092f,0},outer,outer,height*.013f,marble,48);
        }
        mesh.Round(p(0,.922f,0),span*.37f,span*.36f,height*.078f,marble,48);
        // Lean the complete masonry tower without flattening its circular arcades.
        constexpr float slope=.067f;
        for(size_t i=first;i<mesh.vertices.size();++i){
            auto& vertex=mesh.vertices[i];
            vertex.position.x+=slope*vertex.position.y;
            vertex.normal=glm::normalize(V(vertex.normal.x,vertex.normal.y-slope*vertex.normal.x,vertex.normal.z));
        }
        break;
    }
    case LandmarkForm::Kaaba: {
        mesh.Box({0,-footing,0},{width*1.08f,height*.047f+footing,depth*1.08f},marble);
        const Paint cloth(.034f,.031f,.027f,0);
        mesh.Box(p(0,.047f,0),p(1,.953f,1),cloth);
        mesh.Box(p(0,.70f,0),p(1.01f,.062f,1.01f),bronze);
        mesh.Box(p(.26f,.145f,.503f),p(.14f,.30f,.012f),bronze);
        break;
    }
    case LandmarkForm::Skyscraper: {
        mesh.Box({0,-footing,0},{width,height*.53f+footing,depth},windowGlass);
        mesh.Box(p(0,.53f,0),p(.78f,.25f,.78f),windowGlass);
        mesh.Box(p(0,.78f,0),p(.53f,.155f,.53f),windowGlass);
        for(int i=1;i<=44;++i){
            const float y=i*.0209f;
            const float band=y<.53f?1.017f:y<.78f?.798f:.544f;
            mesh.Box(p(0,y,0),p(band,.003f,band),oldStone);
        }
        mesh.Round(p(0,.929f,0),span*.065f,span*.009f,height*.071f,ivory,12);
        break;
    }
    case LandmarkForm::Tower: {
        mesh.Box({0,-footing,0},{width*.85f,height*.72f+footing,depth*.85f},limestone);
        LandmarkWindows(mesh,{0,0,0},width*.85f,height*.65f,depth*.85f,3,9);
        for(float level:{.10f,.42f,.68f,.80f})
            mesh.Box(p(0,level,0),p(.93f,.026f,.93f),ivory);
        mesh.Box(p(0,.72f,0),p(.72f,.105f,.72f),oldStone);
        LandmarkRoof(mesh,p(0,.825f,0),width*.84f,height*.145f,depth*.84f,charcoal);
        mesh.Round(p(0,.969f,0),span*.032f,span*.008f,height*.031f,bronze,10);
        break;
    }
    case LandmarkForm::Dome:
    case LandmarkForm::Mosque: {
        const bool mosque=form==LandmarkForm::Mosque;
        mesh.Box({0,-footing,0},{width*.89f,height*.35f+footing,depth*.82f},ivory);
        LandmarkWindows(mesh,{0,0,0},width*.89f,height*.30f,depth*.82f,7,2);
        mesh.Round(p(0,.35f,0),span*.32f,span*.32f,height*.09f,paleStone,32);
        LandmarkEllipsoid(mesh,p(0,.44f,0),V(span*.33f,height*.44f,span*.33f),
            mosque?glazedTile:copperPatina,true);
        mesh.Round(p(0,.88f,0),span*.032f,span*.008f,height*.12f,bronze,12);
        if(mosque)for(int x:{-1,1})for(int z:{-1,1}){
            const V base=p(.44f*x,0,.37f*z);
            LandmarkSpire(mesh,base,span*.058f,height*.90f,ivory,glazedTile);
            mesh.Round(base+V(0,height*.59f,0),span*.071f,span*.071f,height*.037f,paleStone,16);
        }
        break;
    }
    case LandmarkForm::Cathedral: {
        mesh.Box({0,-footing,0},{width*.73f,height*.48f+footing,depth*.88f},ivory);
        LandmarkWindows(mesh,{0,0,0},width*.73f,height*.42f,depth*.88f,5,2);
        LandmarkRoof(mesh,p(0,.48f,0),width*.78f,height*.18f,depth*.91f,charcoal);
        for(int side:{-1,1}){
            const V tower=p(.35f*side,0,.29f);
            mesh.Box(tower-V(0,footing,0),{width*.25f,height*.69f+footing,depth*.25f},limestone);
            mesh.Box(tower+p(0,.65f,0),p(.278f,.05f,.278f),ivory);
            mesh.Round(tower+p(0,.70f,0),span*.127f,span*.010f,height*.30f,charcoal,8);
            for(int i=0;i<3;++i){
                const V foot=p(.47f*side,0,-.23f+.16f*i),top=p(.35f*side,.41f,-.23f+.16f*i);
                LandmarkBeam(mesh,foot,top,span*.044f,ivory);
            }
        }
        mesh.Arch(p(0,0,.45f),{1,0,0},width*.084f,height*.16f,width*.052f,depth*.03f,paleStone);
        break;
    }
    case LandmarkForm::Palace:
    case LandmarkForm::House: {
        const bool palace=form==LandmarkForm::Palace;
        const Paint wall=palace?ivory:paleStone;
        mesh.Box({0,-footing,0},{width,height*.68f+footing,depth},wall);
        LandmarkRoof(mesh,p(0,.68f,0),width*1.043f,height*.24f,depth*1.05f,palace?charcoal:roofTile);
        LandmarkWindows(mesh,{0,0,0},width,height*.65f,depth,palace?13:4,palace?3:2);
        mesh.Box(p(0,0,.505f),p(.12f,.28f,.015f),darkStone);
        if(palace){
            for(int i=-3;i<=3;++i)
                mesh.Round(p(i*.065f,0,.61f),span*.030f,span*.024f,height*.58f,ivory,16);
            mesh.Box(p(0,.525f,.583f),p(.49f,.055f,.147f),paleStone);
            LandmarkRoof(mesh,p(0,.58f,.54f),width*.51f,height*.115f,depth*.24f,charcoal);
        }else{
            mesh.Box(p(-.29f,.72f,-.20f),p(.091f,.28f,.13f),roofTile);
        }
        break;
    }
    case LandmarkForm::Temple: {
        for(int i=0;i<4;++i){
            const float scale=1.f-i*.065f;
            mesh.Box(p(0,i*.038f,0),p(scale,.04f,scale),limestone);
        }
        mesh.Box(p(0,.15f,0),p(.55f,.51f,.55f),sandstone);
        for(int side:{-1,1})for(int i=-3;i<=3;++i)
            mesh.Round(p(i*.107f,.15f,.36f*side),span*.048f,span*.041f,height*.51f,ivory,16);
        mesh.Box(p(0,.65f,0),p(.82f,.08f,.82f),paleStone);
        LandmarkRoof(mesh,p(0,.73f,0),width*.83f,height*.27f,depth*.86f,roofTile);
        break;
    }
    case LandmarkForm::Pagoda: {
        mesh.Box({0,-footing,0},{width,height*.075f+footing,depth},limestone);
        for(int i=0;i<5;++i){
            const float size=1.f-i*.134f,y=.075f+i*.164f;
            mesh.Box(p(0,y,0),p(.66f*size,.112f,.66f*size),roofTile);
            mesh.Box(p(0,y+.103f,0),p(.88f*size,.015f,.88f*size),charcoal);
            mesh.SquareStage(p(0,y+.113f,0),span*.94f*size,span*.56f*size,height*.061f,glazedTile);
        }
        mesh.Round(p(0,.90f,0),span*.075f,span*.012f,height*.10f,bronze,12);
        break;
    }
    case LandmarkForm::Pyramid: {
        constexpr int courses=24;
        for(int i=0;i<courses;++i){
            const float fraction=float(i)/courses;
            const float initial=span*(1.f-fraction*.98f);
            const float end=span*(1.f-(fraction+.94f/courses)*.98f);
            mesh.SquareStage(p(0,fraction,0),initial,end,height/courses,i%3?limestone:paleStone);
        }
        break;
    }
    case LandmarkForm::Windmill: {
        mesh.Round({0,-footing,0},span*.32f,span*.24f,height*.69f+footing,ivory,24);
        mesh.Round(p(0,.69f,0),span*.27f,span*.066f,height*.22f,charcoal,24);
        const V hub=p(0,.64f,.29f);
        for(int i=0;i<4;++i){
            const float angle=(i+.32f)*glm::half_pi<float>();
            const V vector(std::cos(angle)*width*.48f,std::sin(angle)*height*.36f,0);
            const V bladeNormal=glm::normalize(glm::cross(vector,V(0,0,1)));
            LandmarkBeam(mesh,hub,hub+vector,span*.026f,oldStone);
            const V edge=bladeNormal*span*.095f;
            const V a=hub+vector*.28f,b=hub+vector;
            mesh.Quad(a,b,b+edge,a+edge,ivory,{0,0,1});
            for(int strut=3;strut<=9;++strut){
                const V point=hub+vector*(strut*.10f);
                LandmarkBeam(mesh,point,point+edge,span*.012f,oldStone);
            }
        }
        break;
    }
    case LandmarkForm::Arch: {
        const float radius=std::min(width*.26f,height*.30f);
        const float face=width*.092f;
        mesh.Arch({0,0,0},{1,0,0},radius,height*.50f,face,depth*.82f,ivory);
        for(int side:{-1,1}){
            mesh.Box({side*(radius+face*.86f),-footing,0},{face*.81f,height*.72f+footing,depth*.9f},ivory);
            mesh.Box({side*(radius+face*.86f),0,0},{face*1.05f,height*.12f,depth},limestone);
        }
        const float crown=std::max(height*.79f,height*.50f+radius+face*.85f);
        mesh.Box({0,crown,0},{width, std::max(height*.09f,height-crown),depth},paleStone);
        break;
    }
    case LandmarkForm::Obelisk:
    case LandmarkForm::Monument: {
        const bool obelisk=form==LandmarkForm::Obelisk;
        mesh.Box({0,-footing,0},{width,height*.12f+footing,depth},limestone);
        mesh.SquareStage(p(0,.12f,0),span*(obelisk?.83f:.64f),span*(obelisk?.61f:.37f),height*.75f,marble);
        mesh.SquareStage(p(0,.87f,0),span*(obelisk?.61f:.42f),span*(obelisk?.012f:.19f),height*.13f,
            obelisk?paleStone:bronze);
        break;
    }
    }
}
