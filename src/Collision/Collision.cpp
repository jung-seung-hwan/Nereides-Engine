#include "Collision/Collision.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace nereides
{
namespace
{
using V=DirectX::XMFLOAT3;
V Sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V Add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V Mul(V a,float s){return {a.x*s,a.y*s,a.z*s};}
double Dot(V a,V b){return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;}
bool Finite(V v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
std::optional<double> Quadratic(double a,double b,double c,double low,double high)
{
    if(a*low*low+b*low+c<=0) return low;
    if(a<1e-20) return {};
    const double discriminant=b*b-4*a*c;
    if(discriminant<0) return {};
    const double root=(-b-std::sqrt(discriminant))/(2*a);
    if(root>=low && root<=high) return root;
    return {};
}
std::optional<double> Slab(V start,V delta,V extents)
{
    double lo=0,hi=1;
    const float s[]={start.x,start.y,start.z},d[]={delta.x,delta.y,delta.z},e[]={extents.x,extents.y,extents.z};
    for(unsigned i=0;i<3;++i)
    {
        if(std::abs(d[i])<1e-12f) {if(std::abs(s[i])>e[i]) return {};continue;}
        double a=(-double(e[i])-s[i])/d[i],b=(double(e[i])-s[i])/d[i];
        if(a>b) std::swap(a,b); lo=std::max(lo,a);hi=std::min(hi,b);
        if(lo>hi) return {};
    }
    return lo;
}
// Exact linear sweep of a sphere against an axis-aligned box, including rounded corners.
std::optional<double> SphereBox(V start,V delta,V extent,float radius)
{
    const double s[]={start.x,start.y,start.z},d[]={delta.x,delta.y,delta.z},e[]={extent.x,extent.y,extent.z};
    std::vector<double> cuts{0,1};
    for(unsigned i=0;i<3;++i) if(std::abs(d[i])>1e-12)
        for(double sign:{-1.,1.}) {double t=(sign*e[i]-s[i])/d[i];if(t>0 && t<1) cuts.push_back(t);}
    std::sort(cuts.begin(),cuts.end());
    for(std::size_t n=1;n<cuts.size();++n)
    {
        double a=0,b=0,c=-double(radius)*radius,mid=(cuts[n-1]+cuts[n])*.5;
        for(unsigned i=0;i<3;++i)
        {
            const double p=s[i]+d[i]*mid;
            if(p>=-e[i] && p<=e[i]) continue;
            const double offset=s[i]-(p<0 ? -e[i] : e[i]);
            a+=d[i]*d[i];b+=2*offset*d[i];c+=offset*offset;
        }
        if(auto t=Quadratic(a,b,c,cuts[n-1],cuts[n])) return t;
    }
    return {};
}
std::optional<double> Sweep(const CollisionShape& a,const CollisionShape& b,V previousA,V previousB)
{
    V start=Sub(previousA,previousB),delta=Sub(Sub(a.center,previousA),Sub(b.center,previousB));
    if(a.shape==Shape::Sphere && b.shape==Shape::Sphere)
    {double r=double(a.radius)+b.radius;return Quadratic(Dot(delta,delta),2*Dot(start,delta),Dot(start,start)-r*r,0,1);}
    if(a.shape==Shape::Box && b.shape==Shape::Box) return Slab(start,delta,Add(a.halfExtents,b.halfExtents));
    if(a.shape==Shape::Sphere) return SphereBox(start,delta,b.halfExtents,a.radius);
    return SphereBox(Mul(start,-1),Mul(delta,-1),a.halfExtents,b.radius);
}
bool SameGeometry(const CollisionShape& a,const CollisionShape& b)
{
    return a.shape==b.shape && a.radius==b.radius && a.halfExtents.x==b.halfExtents.x &&
        a.halfExtents.y==b.halfExtents.y && a.halfExtents.z==b.halfExtents.z;
}
}
std::vector<CollisionShape> CollisionWorld::Collect(const Scene& scene) const
{
    using namespace DirectX;
    std::vector<CollisionShape> result;
    for(auto id:scene.Objects())
    {
        const auto* collider=scene.Find(id)->Get<Collider>();
        if(!scene.Active(id) || !collider || !collider->enabled) continue;
        CollisionShape shape;shape.object=id;shape.shape=collider->shape;shape.layer=collider->layer;shape.mask=collider->mask;
        shape.trigger=collider->trigger;shape.attack=collider->attack;
        const auto world=scene.World(id);XMFLOAT4X4 m;XMStoreFloat4x4(&m,world);
        XMStoreFloat3(&shape.center,XMVector3TransformCoord(XMLoadFloat3(&collider->offset),world));
        const auto& e=collider->halfExtents;
        shape.halfExtents={std::abs(m._11)*e.x+std::abs(m._21)*e.y+std::abs(m._31)*e.z,
            std::abs(m._12)*e.x+std::abs(m._22)*e.y+std::abs(m._32)*e.z,
            std::abs(m._13)*e.x+std::abs(m._23)*e.y+std::abs(m._33)*e.z};
        // Max singular value upper bound under parent shear; exact max scale for orthogonal TRS axes.
        const auto x=world.r[0],y=world.r[1],z=world.r[2];
        const float scale=std::sqrt(std::max({XMVectorGetX(XMVector3Dot(x,x))+std::abs(XMVectorGetX(XMVector3Dot(x,y)))+std::abs(XMVectorGetX(XMVector3Dot(x,z))),
            XMVectorGetX(XMVector3Dot(y,y))+std::abs(XMVectorGetX(XMVector3Dot(y,x)))+std::abs(XMVectorGetX(XMVector3Dot(y,z))),
            XMVectorGetX(XMVector3Dot(z,z))+std::abs(XMVectorGetX(XMVector3Dot(z,x)))+std::abs(XMVectorGetX(XMVector3Dot(z,y)))}));
        shape.radius=collider->radius*scale;
        if(!Finite(shape.center)||!Finite(shape.halfExtents)||!std::isfinite(shape.radius)||collider->radius<0||e.x<0||e.y<0||e.z<0)
            throw std::invalid_argument("Invalid collider shape");
        result.push_back(shape);
    }
    return result;
}
std::vector<Contact> CollisionWorld::Step(const Scene& scene,double start,double end)
{
    if(!std::isfinite(start)||!std::isfinite(end)||end<start) throw std::invalid_argument("Invalid collision time interval");
    auto shapes=Collect(scene); std::vector<Contact> result;
    std::set<std::pair<ObjectId,ObjectId>> overlaps;
    std::map<ObjectId,CollisionShape> current;
    for(const auto& shape:shapes) current.emplace(shape.object,shape);
    for(std::size_t i=0;i<shapes.size();++i) for(std::size_t j=i+1;j<shapes.size();++j)
    {
        const auto& a=shapes[i];const auto& b=shapes[j];
        if(!(a.mask&b.layer)||!(b.mask&a.layer)) continue;
        auto key=std::minmax(a.object,b.object);
        const std::pair<ObjectId,ObjectId> pair{key.first,key.second};
        const bool trigger=a.trigger||b.trigger;
        const auto pa=m_previous.find(a.object),pb=m_previous.find(b.object);
        const bool tracked=pa!=m_previous.end() && pb!=m_previous.end() && SameGeometry(pa->second,a) && SameGeometry(pb->second,b) &&
            pa->second.attack==a.attack && pb->second.attack==b.attack;
        const V oldA=tracked ? pa->second.center : a.center,oldB=tracked ? pb->second.center : b.center;
        const auto time=Sweep(a,b,oldA,oldB);
        const bool overlapping=Sweep(a,b,a.center,b.center).has_value();
        if(trigger && overlapping) overlaps.insert(pair);
        if(time)
        {
            const double fraction=tracked ? *time : 1.; // New collider exists only at current sample.
            const auto point=Add(oldA,Mul(Sub(a.center,oldA),static_cast<float>(fraction)));
            result.push_back({a.object,b.object,a.attack,b.attack,trigger ? (m_overlaps.contains(pair)?ContactKind::Stay:ContactKind::Enter):ContactKind::Hit,
                fraction,start+(end-start)*fraction,point});
            if(trigger && !overlapping && !m_overlaps.contains(pair))
                result.push_back({a.object,b.object,a.attack,b.attack,ContactKind::Exit,1,end,b.center});
        }
    }
    for(const auto& pair:m_overlaps)
        if(!overlaps.contains(pair) && current.contains(pair.first) && current.contains(pair.second))
            result.push_back({pair.first,pair.second,current.at(pair.first).attack,current.at(pair.second).attack,ContactKind::Exit,1,end,{}});
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){
        if(a.time!=b.time)return a.time<b.time;
        if(a.first!=b.first)return a.first<b.first;
        if(a.second!=b.second)return a.second<b.second;
        return a.kind<b.kind;
    });
    m_previous=std::move(current);m_overlaps=std::move(overlaps);return result;
}
std::optional<RayHit> CollisionWorld::Raycast(const Scene& scene,V origin,V direction,float maxDistance,unsigned mask) const
{
    if(!Finite(origin)||!Finite(direction)||!std::isfinite(maxDistance)||maxDistance<0) return {};
    const double length=std::sqrt(Dot(direction,direction));if(length<1e-12)return {};
    const V delta=Mul(direction,maxDistance/static_cast<float>(length));
    std::optional<RayHit> closest;
    CollisionShape ray;ray.shape=Shape::Sphere;ray.radius=0;ray.center=Add(origin,delta);
    for(const auto& shape:Collect(scene)) if(mask&shape.layer)
        if(auto fraction=Sweep(ray,shape,origin,shape.center))
        {
            const float distance=static_cast<float>(*fraction)*maxDistance;
            if(!closest || distance<closest->distance) closest=RayHit{shape.object,distance,Add(origin,Mul(delta,static_cast<float>(*fraction)))};
        }
    return closest;
}
}
