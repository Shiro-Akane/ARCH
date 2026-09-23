#include "physics/gravity/GravityBoundary.h"
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <unordered_map>
namespace Physical::Gravity {
GravityBoundary::GravityBoundary(const arch::elliptic::CompositePoisson& op):volumes_(op.volumes()) {
    using namespace arch::elliptic;
    if(op.base().dimension!=3) throw std::invalid_argument("Isolated gravity requires three dimensions");
    int root_level=0;
    // Round UP: a 48-cell axis needs a 64-cell tree root. Flooring this
    // depth would silently omit mass in the outermost root cells.
    for(int n=*std::max_element(op.base().cells.begin(),op.base().cells.end());n>1;n=n/2+n%2) --root_level;
    std::unordered_map<CompositeCell,int,CompositeCellHash> lookup;
    std::vector<BoundaryTreeNode> raw;
    const auto ensure=[&](CompositeCell key) {
        auto [entry,added]=lookup.emplace(key,static_cast<int>(raw.size()));
        if(added) {
            BoundaryTreeNode node;
            for(int a=0;a<3;++a) {
                const double width=std::ldexp(op.base().spacing[a],-key.level);
                node.center[a]=op.base().origin[a]+(key.index[a]+0.5)*width;
                node.radius_squared+=0.25*width*width;
            }
            raw.push_back(node);
        }
        return entry->second;
    };
    for(int cell=0;cell<op.size();++cell) {
        auto key=op.cells()[cell];int child=ensure(key);raw[child].cell=cell;
        while(key.level>root_level) {
            const int octant=(key.index[0]&1)+2*(key.index[1]&1)+4*(key.index[2]&1);
            --key.level;for(int a=0;a<3;++a) key.index[a]/=2;
            const int parent=ensure(key);raw[parent].children[octant]=child;child=parent;
        }
    }
    std::function<int(int,int)> flatten=[&](int old,int depth) {
        const int index=static_cast<int>(nodes_.size());nodes_.push_back(raw[old]);
        if(static_cast<int>(layers_.size())<=depth) layers_.resize(depth+1);
        layers_[depth].push_back(index);
        for(int a=0;a<8;++a) if(raw[old].children[a]>=0) {
            const int child=flatten(raw[old].children[a],depth+1);
            nodes_[index].children[a]=child;
        }
        nodes_[index].end=static_cast<int>(nodes_.size());return index;
    };
    flatten(lookup.at({root_level,{0,0,0}}),0);
    moments_.resize(nodes_.size());
}
void GravityBoundary::update(std::span<const double> density) {
    arch::elliptic::validate_values(density,volumes_.size());
    for(auto layer=layers_.rbegin();layer!=layers_.rend();++layer) for(int index:*layer) {
        const int cell=nodes_[index].cell;
        if(cell>=0) {
            if(density[cell]<0.) throw std::invalid_argument("Negative isolated source density");
            moments_[index]={};moments_[index].value[0]=density[cell]*volumes_[cell];
        } else moments_[index]=combine_boundary_moments(nodes_.data(),moments_.data(),index);
        for(double value:moments_[index].value)
            if(!std::isfinite(value)) throw std::overflow_error("Nonfinite isolated mass moment");
    }
}
std::vector<double> GravityBoundary::values(const arch::elliptic::CompositePoisson& op,
                                           double G,double theta,int order) const {
    if(!std::isfinite(G) || G<=0. || !std::isfinite(theta) || theta<0. || theta>=1.
        || order<0 || order>2) throw std::invalid_argument("Invalid isolated boundary evaluation");
    std::vector<double> result(op.faces().size());
    for(std::size_t i=0;i<result.size();++i) if(op.faces()[i].boundary_side>=0) {
        result[i]=isolated_potential(nodes_.data(),moments_.data(),static_cast<int>(nodes_.size()),
                                    op.faces()[i].center.data(),G,theta,order);
        if(!std::isfinite(result[i])) throw std::runtime_error("Nonfinite isolated boundary potential");
    }
    return result;
}
} // namespace Physical::Gravity
