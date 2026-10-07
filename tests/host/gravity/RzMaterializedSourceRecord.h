/**
 * @file RzMaterializedSourceRecord.h
 * @brief Tests-only value record of one actually authenticated Native RZ source.
 *
 * Workflow:
 * 1. Exclusively attach this record to the actual Stage for one synchronous call.
 * 2. Copy the real operator, source density, input identities and observer sites.
 * 3. After the call exits, require exactly one callback and that Stage's source
 *    completion; detach the sink on success and failure, then propagate errors.
 * 4. Optionally use the Stage-owned Current reader and match its issued purpose.
 * 5. Alternatively move the actual Hydro/Current issuer's sealed published
 *    source+field snapshot into this owner without a preparing hook/callback.
 * 6. Emit finite, round-trippable JSON, never a physical/production certificate.
 *
 * Every bound is copied from the actual operator. Source rho is the actual
 * native V-mean interpreted as constant full-ring density. No ideal-spacing
 * reconstruction, rho-from-RHS, synthetic UID or borrowed data is retained.
 */
#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include "amr/elliptic/EllipticMeshAdapter.h"
#include "driver/stages/GravityStage.h"
#include "physics/gravity/GravityBoundary.h"
#include "physics/gravity/self/SelfGravity.h"

namespace arch::test {
/** Own values only. Public completion cannot be supplied as a bool or from an
 * unrelated Stage: capture_call controls attachment, callback and finalization.
 * The caller supplies one genuine prepare invocation and must not modify
 * topology/configuration/leases or change this exclusive sink during the call.
 */
class RzMaterializedSourceRecord final {
    using View=Physical::Gravity::NativeRzSourceInspectionView;
    using Identity=Physical::Gravity::GravitySolveIdentity;
    using OwnedSolution=Physical::Gravity::NativeRzSolutionInspection;
    struct Leaf {
        std::size_t source_index,block;int offset,level;
        std::array<int,3> logical;
        double rl,rh,zl,zh,rho,volume;
        std::array<double,3> center;
    };
    // Preserve the original native operator row, not only a display site.
    using Face=arch::elliptic::CompositeFace;
    struct Patch {
        amr::BlockHandle handle;arch::grid::ScalarFieldLayout layout;
        arch::grid::FieldMemory memory;GridMetrics::GeometryView geometry;
    };
    struct Captured {
        Identity identity;
        std::uint64_t generation;
        std::optional<Physical::Gravity::GravityFieldPurpose> purpose;
        std::uint64_t runtime_lease_generation;
        GravityConfig service;
        arch::elliptic::EllipticMesh mesh;
        std::array<bool,3> periodic;
        std::array<double,4> root;
        std::vector<Leaf> leaves;
        std::vector<Face> faces;
        std::vector<Patch> patches;
    };
    struct Field {
        Physical::Gravity::NativeRzFieldInspection receipt;
        int conditional_status,physical_status;
    };
    std::optional<Captured> captured_;
    std::optional<Field> field_;
    // This sealed value owns its one field; reducers borrow it without recopying.
    std::optional<OwnedSolution> owning_;
    bool published_source_checked_=false;
    std::size_t callbacks_=0;
    bool capturing_=false,checked_=false,invoke_failed_=false;
    std::exception_ptr cleanup_failure_;
    std::string error_type_,error_message_;

    /** Match IEEE values exactly, including signed zero. */
    static bool same(double a,double b) noexcept {
        return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);
    }
    /** Compare complete original face row values by bits; no tolerance or
     * reconstructed geometry is allowed to connect a field to another source.
     */
    static bool same_face(const Face& a,const Face& b) noexcept {
        if(a.left!=b.left||a.right!=b.right||a.axis!=b.axis||a.construction!=b.construction
            ||a.boundary_side!=b.boundary_side||a.native_bounds!=b.native_bounds
            ||!same(a.area,b.area)||!same(a.boundary_coefficient,b.boundary_coefficient)
            ||!same(a.anchor_coefficient,b.anchor_coefficient)
            ||!same(a.value_boundary_coefficient,b.value_boundary_coefficient)
            ||a.samples!=b.samples||a.value_samples!=b.value_samples
            ||a.coefficients.size()!=b.coefficients.size()
            ||a.value_coefficients.size()!=b.value_coefficients.size())return false;
        for(std::size_t n=0;n<3;++n)
            if(!same(a.center[n],b.center[n])||!same(a.fragment_lower[n],b.fragment_lower[n])
                ||!same(a.fragment_upper[n],b.fragment_upper[n])||!same(a.fragment_width[n],b.fragment_width[n]))return false;
        for(std::size_t n=0;n<a.coefficients.size();++n)if(!same(a.coefficients[n],b.coefficients[n]))return false;
        for(std::size_t n=0;n<a.value_coefficients.size();++n)if(!same(a.value_coefficients[n],b.value_coefficients[n]))return false;
        return true;
    }
    /** Refuse NaN/Infinity instead of emitting invalid JSON or substituting zero. */
    static void finite(double x) {
        if(!std::isfinite(x))throw std::domain_error("Nonfinite actual source-record value");
    }
    /** Serialize the actual optional tag without deriving Runtime/science rights. */
    static void write_purpose(std::ostream& out,
            std::optional<Physical::Gravity::GravityFieldPurpose> purpose) {
        using Physical::Gravity::GravityFieldPurpose;
        if(!purpose)out<<"null";
        else if(*purpose==GravityFieldPurpose::AcceptedCurrent)out<<"\"AcceptedCurrent\"";
        else if(*purpose==GravityFieldPurpose::HydroStage)out<<"\"HydroStage\"";
        else throw std::logic_error("Unknown actual source-record purpose");
    }
    /** Check a real stored stencil/face without evaluating or repairing it. */
    static void valid_face(const Face& f,std::size_t count) {
        const auto owned=[&](int index){return index>=0&&std::size_t(index)<count;};
        if((f.left!=-1&&!owned(f.left))||(f.right!=-1&&!owned(f.right))
            ||(f.left==-1&&f.right==-1)||(f.axis!=0&&f.axis!=1)
            ||f.boundary_side< -1||f.boundary_side>3||!f.native_bounds
            ||(f.boundary_side>=0&&f.boundary_side/2!=f.axis)
            ||f.samples.size()!=f.coefficients.size()||f.value_samples.size()!=f.value_coefficients.size())
            throw std::logic_error("Invalid actual native field face/stencil indices");
        finite(f.area);if(f.area<0.)throw std::domain_error("Negative actual native face area");
        finite(f.boundary_coefficient);finite(f.anchor_coefficient);finite(f.value_boundary_coefficient);
        for(double x:f.center)finite(x);for(double x:f.fragment_lower)finite(x);
        for(double x:f.fragment_upper)finite(x);for(double x:f.fragment_width)finite(x);
        for(int index:f.samples)if(!owned(index))throw std::logic_error("Invalid actual gradient sample index");
        for(int index:f.value_samples)if(!owned(index))throw std::logic_error("Invalid actual value sample index");
        for(double x:f.coefficients)finite(x);for(double x:f.value_coefficients)finite(x);
    }
    /** JSON decimal for the original binary64, including recoverable negative zero. */
    static void number(std::ostream& out,double x) {
        finite(x);
        if(x==0.)out<<(std::signbit(x)?"-0.0":"0.0");
        else out<<std::setprecision(std::numeric_limits<double>::max_digits10)<<x;
    }
    /** Escape control characters and accept only well-formed UTF-8 strings.
     * Bytes of scientific values or error text are never silently replaced.
     */
    static void text(std::ostream& out,std::string_view value) {
        constexpr char hex[]="0123456789abcdef";
        out<<'"';
        for(std::size_t p=0;p<value.size();) {
            const unsigned char c=static_cast<unsigned char>(value[p]);
            if(c<0x80) {
                if(c=='"'||c=='\\')out<<'\\'<<char(c);
                else if(c<0x20)out<<"\\u00"<<hex[c>>4]<<hex[c&15];
                else out<<char(c);
                ++p;continue;
            }
            const int width=c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
            if(!width||p+std::size_t(width)>value.size())throw std::domain_error("Invalid UTF-8 in source record");
            std::uint32_t code=c&((1u<<(7-width))-1u);
            for(int k=1;k<width;++k) {
                const auto continuation=static_cast<unsigned char>(value[p+std::size_t(k)]);
                if((continuation&0xc0)!=0x80)throw std::domain_error("Invalid UTF-8 continuation in source record");
                code=(code<<6)|(continuation&63);
            }
            if(code<(width==2?0x80u:width==3?0x800u:0x10000u)
                ||code>0x10ffff||(code>=0xd800&&code<=0xdfff))
                throw std::domain_error("Invalid UTF-8 scalar in source record");
            out.write(value.data()+p,width);p+=std::size_t(width);
        }
        out<<'"';
    }
    /** Emit an actual floating array; dimensions and order are retained. */
    template<std::size_t N> static void doubles(std::ostream& out,const std::array<double,N>& a) {
        out<<'[';for(std::size_t n=0;n<N;++n){if(n)out<<',';number(out,a[n]);}out<<']';
    }
    /** Emit integer metadata without passing uint64 identities through double. */
    template<class T,std::size_t N> static void integers(std::ostream& out,const std::array<T,N>& a) {
        out<<'[';for(std::size_t n=0;n<N;++n){if(n)out<<',';out<<a[n];}out<<']';
    }
    /** Serialize a complete actual coefficient/index vector in its stored order. */
    static void values(std::ostream& out,const std::vector<double>& a) {
        out<<'[';for(std::size_t n=0;n<a.size();++n){if(n)out<<',';number(out,a[n]);}out<<']';
    }
    /** Serialize original stencil cell indices without inventing an interpolation. */
    static void indices(std::ostream& out,const std::vector<int>& a) {
        out<<'[';for(std::size_t n=0;n<a.size();++n){if(n)out<<',';out<<a[n];}out<<']';
    }
    /** Describe an actual source dependency; no pool index is called a UID. */
    static void input(std::ostream& out,const Physical::Gravity::GravityInputIdentity& x) {
        out<<"{\"uid\":"<<x.block.uid.value<<",\"epoch\":"<<x.block.epoch.value
            <<",\"slot\":"<<int(x.slot)<<",\"version\":"<<x.version.value
            <<",\"storage_generation\":"<<x.storage_generation<<'}';
    }
    /** Opaque label composed only from actual stamps; not a fabricated counter,
     * hash, globally unique run identity or substitute for the outer ELF record.
     */
    std::string source_id() const {
        const auto& c=*captured_;std::ostringstream out;out.imbue(std::locale::classic());
        out<<"materialized-native-rz:epoch="<<c.identity.topology.value<<":ring="<<c.generation
            <<":operator="<<c.identity.operator_revision<<":boundary="<<c.identity.boundary_revision
            <<":accuracy="<<c.identity.accuracy_revision<<":time=";number(out,c.identity.input_time);
        for(const auto& i:c.identity.inputs)out<<":uid="<<i.block.uid.value<<":epoch="<<i.block.epoch.value
            <<":slot="<<int(i.slot)<<":version="<<i.version.value<<":lease="<<i.storage_generation;
        return out.str();
    }
    /** Source leaf identity retains the actual block UID, epoch and logical cell. */
    std::string leaf_id(const Leaf& leaf) const {
        const auto& h=captured_->identity.inputs.at(leaf.block).block;
        std::ostringstream out;out.imbue(std::locale::classic());
        out<<"uid="<<h.uid.value<<":epoch="<<h.epoch.value<<":level="<<leaf.level
            <<":cell="<<leaf.logical[0]<<','<<leaf.logical[1]<<','<<leaf.logical[2];return out.str();
    }
    /** Capture once, and only while this record is attached by capture_call.
     * Build all value copies provisionally; an allocation/input failure cannot
     * leave a partially final record or change any real source/field array.
     */
    void capture(const View& view) {
        if(!capturing_||++callbacks_!=1)throw std::logic_error("Source record requires one real attached callback");
        Physical::Gravity::validate_gravity_solve_identity(view.request.identity);
        if(!view.source_generation||view.op.size()<=0||!view.op.base().native_canonical_domain
            ||view.op.base().dimension!=2||view.op.base().semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
            ||view.density.size()!=std::size_t(view.op.size())||view.binding.storage.size()!=view.density.size()
            ||view.binding.cells!=view.op.cells()||view.binding.handles.size()!=view.request.identity.inputs.size()
            ||view.binding.grids.size()!=view.binding.handles.size()
            ||view.request.blocks.size()!=view.binding.handles.size())
            throw std::logic_error("Malformed actual native source record view");
        const auto purpose=view.request.purpose;
        const auto* const lease=view.request.runtime_lease;
        if((purpose&&!Physical::Gravity::valid_gravity_field_purpose(*purpose))
            ||(lease&&(!purpose||lease->purpose()!=*purpose||!lease->generation())))
            throw std::logic_error("Source record lost its actual preparing purpose/issuer");
        // Preparing tokens are intentionally unsealed. The real Stage performs
        // pre/callback/post authentication; capture_call finalizes only that
        // Stage's completed callback, never a caller-provided success flag.
        Captured next{view.request.identity,view.source_generation,purpose,
            lease?lease->generation():0,view.service_configuration,
            view.op.base(),view.binding.periodic,{view.op.base().origin[0],view.op.base().root_upper[0],
                view.op.base().origin[1],view.op.base().root_upper[1]},{},{},{}};
        for(double x:next.root)finite(x);
        for(double x:{next.service.g_x,next.service.g_y,next.service.g_z,next.service.relative_tolerance,
                next.service.absolute_tolerance,next.identity.input_time,next.identity.gravitational_constant})finite(x);
        next.patches.reserve(view.binding.grids.size());
        for(std::size_t b=0;b<view.binding.grids.size();++b) {
            if(!view.binding.grids[b]||view.binding.handles[b]!=next.identity.inputs[b].block)
                throw std::logic_error("Source record lost actual patch identity");
            next.patches.push_back({view.binding.handles[b],view.request.blocks[b].density.layout,
                view.request.blocks[b].density.memory,GridMetrics::make_geometry_view(*view.binding.grids[b],next.mesh.semantics)});
        }
        next.leaves.reserve(view.density.size());
        for(std::size_t n=0;n<view.density.size();++n) {
            const auto& where=view.binding.storage[n];const auto& key=view.op.cells()[n];
            if(where.block>=next.patches.size())throw std::logic_error("Invalid actual source block mapping");
            Leaf leaf{n,where.block,where.offset,key.level,key.index,view.op.lower(int(n),0),view.op.upper(int(n),0),
                view.op.lower(int(n),1),view.op.upper(int(n),1),view.density[n],view.op.volumes().at(n),view.op.center(int(n))};
            for(double x:{leaf.rl,leaf.rh,leaf.zl,leaf.zh,leaf.rho,leaf.volume})finite(x);
            for(double x:leaf.center)finite(x);
            if(!(leaf.rl>=0.&&leaf.rh>leaf.rl&&leaf.zh>leaf.zl&&leaf.rho>=0.&&leaf.volume>0.))
                throw std::domain_error("Invalid actual full-ring source cell");
            // True zero density is copied if actually supplied; no absent cell
            // becomes zero and no positive production density is removed.
            next.leaves.push_back(leaf);
        }
        next.faces.reserve(view.op.faces().size());
        for(const auto& f:view.op.faces()) {
            valid_face(f,next.leaves.size());next.faces.push_back(f);
        }
        captured_.emplace(std::move(next));
    }
    /** Import only a sealed published source/field value, never a preparing View.
     * Workflow: validate the issuer's copied identity and extents -> copy actual
     * cell/patch geometry into the common serializer values -> retain the sealed
     * value by move. No callback, prepare call, solve or field download occurs.
     * rho is the SAME source V-mean; Phi remains the actual cell-center value.
     */
    static Captured owned_source(const OwnedSolution& solution) {
        using Physical::Gravity::GravityFieldPurpose;
        const auto& receipt=solution.field();const auto& mesh=solution.mesh();
        const auto& cells=solution.cells();const auto& patches=solution.patches();
        const auto& rho=solution.density();
        Physical::Gravity::validate_gravity_solve_identity(receipt.source);
        if(!receipt.purpose||(*receipt.purpose!=GravityFieldPurpose::HydroStage
                &&*receipt.purpose!=GravityFieldPurpose::AcceptedCurrent)
            ||!receipt.runtime_lease_authenticated||!receipt.runtime_lease_generation
            ||!receipt.source_generation||!receipt.field_generation||!receipt.physical_status
            ||!mesh.native_canonical_domain||mesh.dimension!=2
            ||mesh.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
            ||rho.empty()||cells.size()!=rho.size()||patches.empty()
            ||patches.size()!=receipt.source.inputs.size()
            ||rho.size()>std::numeric_limits<std::size_t>::max()/6
            ||receipt.potential.size()!=rho.size()||receipt.side_acceleration.size()!=6*rho.size()
            ||receipt.face_gradient.size()!=receipt.faces.size()
            ||receipt.boundary_values.size()!=receipt.faces.size())
            throw std::logic_error("Malformed sealed native source/field snapshot");
        Captured next{receipt.source,receipt.source_generation,receipt.purpose,
            receipt.runtime_lease_generation,solution.service_configuration(),mesh,solution.periodic(),
            {mesh.origin[0],mesh.root_upper[0],mesh.origin[1],mesh.root_upper[1]},{},{},{}};
        for(double x:next.root)finite(x);
        for(double x:{next.service.g_x,next.service.g_y,next.service.g_z,next.service.relative_tolerance,
                next.service.absolute_tolerance,next.identity.input_time,next.identity.gravitational_constant})finite(x);
        next.patches.reserve(patches.size());
        for(std::size_t b=0;b<patches.size();++b) {
            const auto& patch=patches[b];
            if(patch.block!=next.identity.inputs[b].block||patch.memory!=arch::grid::FieldMemory::Host
                ||patch.layout.dimension!=2||patch.layout.centering!=arch::grid::FieldCentering::Cell
                ||patch.geometry.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
                ||patch.geometry.dim!=2||!patch.geometry.dyadic_identity.bound
                ||!GridMetrics::matches_identity(patch.geometry))
                throw std::logic_error("Sealed source snapshot lost its actual native patch identity");
            next.patches.push_back({patch.block,patch.layout,patch.memory,patch.geometry});
        }
        next.leaves.reserve(cells.size());
        for(std::size_t n=0;n<cells.size();++n) {
            const auto& cell=cells[n];
            if(cell.block>=patches.size()||cell.offset<0
                ||cell.offset>=patches[cell.block].geometry.total_size)
                throw std::logic_error("Sealed source cell is outside its actual patch storage");
            Leaf leaf{n,cell.block,cell.offset,cell.key.level,cell.key.index,
                cell.lower[0],cell.upper[0],cell.lower[1],cell.upper[1],rho[n],
                cell.operator_volume,cell.center};
            for(double x:cell.lower)finite(x);for(double x:cell.upper)finite(x);
            for(double x:leaf.center)finite(x);
            for(double x:{leaf.rl,leaf.rh,leaf.zl,leaf.zh,leaf.rho,leaf.volume})finite(x);
            if(!(leaf.rl>=0.&&leaf.rh>leaf.rl&&leaf.zh>leaf.zl&&leaf.rho>0.&&leaf.volume>0.))
                throw std::domain_error("Invalid sealed actual full-ring source cell");
            next.leaves.push_back(leaf);finite(receipt.potential[n]);
            for(const auto& axis:receipt.acceleration) {
                if(axis.size()!=rho.size())throw std::logic_error("Sealed acceleration extent mismatch");
                finite(axis[n]);
            }
        }
        for(double x:receipt.side_acceleration)finite(x);
        // The sealed field already owns every original face row. Validate
        // those rows here and borrow them during JSON, without a second copy.
        for(std::size_t f=0;f<receipt.faces.size();++f) {
            valid_face(receipt.faces[f],rho.size());finite(receipt.face_gradient[f]);
            finite(receipt.boundary_values[f]);
        }
        // Validate the original same-field proof, not a re-evaluated residual or
        // a replacement science criterion. This provisional string is discarded.
        std::ostringstream certificate;write_native_discrete_certificate(certificate,receipt);
        return next;
    }
    /** Both v2 identity headers use the SAME actual issued purpose and lease. */
    static const char* published_authority_scope(const Physical::Gravity::NativeRzFieldInspection& receipt) {
        using Physical::Gravity::GravityFieldPurpose;
        if(!receipt.purpose||!receipt.runtime_lease_authenticated||!receipt.runtime_lease_generation)
            throw std::logic_error("Published source has no authentic Runtime purpose/lease");
        if(*receipt.purpose==GravityFieldPurpose::HydroStage)return "hydro-stage-native-candidate-field-only";
        if(*receipt.purpose==GravityFieldPurpose::AcceptedCurrent)return "accepted-current-numerical-field-only";
        throw std::logic_error("Unknown published source authority scope");
    }
    /** Real Stage callback entry; the payload is owned by the calling fixture. */
    static void sink(void* payload,const View& view) {
        if(!payload)throw std::logic_error("Missing source record payload");
        static_cast<RzMaterializedSourceRecord*>(payload)->capture(view);
    }
public:
    /** Keep this diagnostic/callback owner at one address for its lifetime.
     * A record owns its sealed snapshot and lends references to reducers; moving
     * or partially assigning it would invalidate those references/flags. Source
     * snapshots themselves remain copy/move constructible through their issuer.
     */
    RzMaterializedSourceRecord()=default;
    RzMaterializedSourceRecord(const RzMaterializedSourceRecord&)=delete;
    RzMaterializedSourceRecord(RzMaterializedSourceRecord&&)=delete;
    RzMaterializedSourceRecord& operator=(const RzMaterializedSourceRecord&)=delete;
    RzMaterializedSourceRecord& operator=(RzMaterializedSourceRecord&&)=delete;
    /** Exclusively inspect one actual prepare call. No Stage/View reference is
     * stored. Solve errors propagate after retaining their original type/text;
     * even a later solve failure can have a separately checked materialization.
     * Cleanup errors revoke record qualification and are never silently ignored.
     */
    template<class Invoke> void capture_call(arch::driver::GravityStage& stage,Invoke&& invoke) {
        if(capturing_)throw std::logic_error("Source record call is already active");
        captured_.reset();field_.reset();owning_.reset();published_source_checked_=false;
        callbacks_=0;checked_=false;invoke_failed_=false;
        cleanup_failure_=nullptr;error_type_.clear();error_message_.clear();
        if(stage.native_rz_source_inspection_attached())throw std::logic_error("Actual Stage already owns another source sink");
        stage.set_native_rz_source_inspection(&sink,this);capturing_=true;
        // The only Stage borrow is this local, nonmoving cleanup scope. It
        // detaches even if copying exception text or finalization allocates.
        struct Detach {
            arch::driver::GravityStage& stage;RzMaterializedSourceRecord& record;
            bool attached=true;
            Detach(arch::driver::GravityStage& s,RzMaterializedSourceRecord& r) noexcept:stage(s),record(r){}
            Detach(const Detach&)=delete;Detach(Detach&&)=delete;
            void close() {
                record.capturing_=false;
                stage.set_native_rz_source_inspection(nullptr,nullptr);attached=false;
            }
            ~Detach() {
                if(!attached)return;
                record.checked_=false;record.capturing_=false;
                try{close();}catch(...){record.cleanup_failure_=std::current_exception();}
            }
        } detach{stage,*this};
        std::exception_ptr failure;
        try{std::forward<Invoke>(invoke)();}catch(...){failure=std::current_exception();}
        capturing_=false;invoke_failed_=bool(failure);
        checked_=callbacks_==1&&captured_.has_value()&&stage.native_rz_source_inspection_completed();
        if(failure) {
            try{std::rethrow_exception(failure);}
            catch(const std::exception& error){error_type_=typeid(error).name();error_message_=error.what();}
            catch(...){error_type_="non_std_exception";error_message_="Non-standard solve invocation exception";}
        }
        try{detach.close();}
        catch(...){checked_=false;cleanup_failure_=std::current_exception();if(failure)std::rethrow_exception(failure);throw;}
        if(failure)std::rethrow_exception(failure);
        if(!checked_)throw std::logic_error("Actual Stage did not complete this one source inspection");
    }
    /** Copy a sealed value when the caller intentionally retains its snapshot.
     * This convenience overload performs a value copy, not a solve/download.
     * Real homology uses the rvalue overload so its field has exactly one owner.
     */
    void capture_owned_solution(const OwnedSolution& solution) {
        capture_owned_solution(OwnedSolution(solution));
    }
    /** Move the one actual published snapshot into this record.
     * Build common geometry values provisionally; publish the sealed owner only
     * after validation. This path neither attaches the legacy pre-solve hook
     * nor supplies a callback-completion counter or source-only grant.
     */
    void capture_owned_solution(OwnedSolution&& solution) {
        if(capturing_)throw std::logic_error("Cannot replace an active source callback record");
        auto next=owned_source(solution);
        published_source_checked_=false;checked_=false;
        captured_.emplace(std::move(next));owning_.emplace(std::move(solution));field_.reset();
        callbacks_=0;invoke_failed_=false;cleanup_failure_=nullptr;
        error_type_.clear();error_message_.clear();published_source_checked_=true;
    }
    /** Published inspection is distinct from legacy preparing-source completion. */
    bool published_source_checked() const noexcept {
        return !capturing_&&published_source_checked_&&captured_.has_value()&&owning_.has_value();
    }
    /** Source-only authentication status; no physical or field success meaning. */
    bool source_only_checked() const noexcept {return checked_;}
    /** Cleanup failure is retained for the runner; such a record is unqualified.
     * Supported capture_call invokes a quiescent synchronous prepare. A caller
     * leaving an unrelated active transaction must repair its actual owner;
     * the helper neither bypasses the Core setter nor swallows that rejection.
     */
    bool cleanup_failed() const noexcept {return bool(cleanup_failure_);}
    /** Number of real callbacks, including any rejected duplicate attempt. */
    std::size_t callback_count() const noexcept {return callbacks_;}
    /** Copy one actual owning CPU field receipt after source-call exit.
     * Match source/field generations and the complete original face rows before
     * retaining arrays; no separate Phi/g getter, stencil evaluation, gather or
     * normalization is substituted for the actual published workspace values.
     */
    void capture_native_field(const arch::driver::GravityStage& stage) {
        if(capturing_||published_source_checked_||!checked_||!captured_||invoke_failed_||field_)
            throw std::logic_error("Field record requires one completed source/solve call");
        // One owning receipt from the actual issuer. Do not separately read the
        // SelfGravity workspace/assessment or infer authority from an enum tag.
        auto receipt=stage.native_current_field();
        const auto& identity=captured_->identity;
        if(receipt.source!=identity||!same(receipt.source.input_time,identity.input_time)
            ||!same(receipt.source.gravitational_constant,identity.gravitational_constant)
            ||receipt.source_generation!=captured_->generation||!receipt.field_generation
            ||captured_->purpose!=Physical::Gravity::GravityFieldPurpose::AcceptedCurrent
            ||!captured_->runtime_lease_generation||receipt.purpose!=captured_->purpose
            ||receipt.runtime_lease_generation!=captured_->runtime_lease_generation
            ||!receipt.runtime_lease_authenticated||!receipt.physical_status)
            throw std::logic_error("Actual field receipt belongs to another source/publication");
        const std::size_t n=captured_->leaves.size(),m=captured_->faces.size();
        if(n>std::numeric_limits<std::size_t>::max()/6||receipt.potential.size()!=n
            ||receipt.faces.size()!=m||receipt.face_gradient.size()!=m||receipt.boundary_values.size()!=m
            ||receipt.side_acceleration.size()!=6*n)
            throw std::logic_error("Actual owning field receipt size mismatch");
        for(double x:receipt.potential)finite(x);
        for(double x:receipt.face_gradient)finite(x);for(double x:receipt.boundary_values)finite(x);
        for(double x:receipt.side_acceleration)finite(x);
        for(const auto& axis:receipt.acceleration) {
            if(axis.size()!=n)throw std::logic_error("Actual candidate acceleration size mismatch");
            for(double x:axis)finite(x);
        }
        for(std::size_t f=0;f<m;++f) {
            valid_face(receipt.faces[f],n);
            if(!same_face(receipt.faces[f],captured_->faces[f]))
                throw std::logic_error("Actual field receipt changed an original source face/stencil");
        }
        // Both statuses belong to this one authenticated owning receipt. They
        // remain observations, never a continuum/physical qualification grant.
        const int conditional_status=int(receipt.conditional_residual.status);
        const int physical_status=int(*receipt.physical_status);
        field_.emplace(Field{std::move(receipt),conditional_status,physical_status});
    }
    /** Borrow the SAME already-authenticated owning field receipt.
     * The sealed path retains its actual Hydro or Current purpose; the legacy
     * preparing-source path still obtains a Current-only field from its Stage.
     * Workflow: require completed source/solve capture -> require retained field
     * -> lend a const reference for immediate tests-only reducers/serialization.
     * Its lifetime ends with this record; no new solve, download, reduction,
     * Runtime lease, physical qualification or pre-Hydro source View is issued.
     */
    const Physical::Gravity::NativeRzFieldInspection& native_field_receipt() const {
        if(published_source_checked())return owning_->field();
        if(capturing_||!checked_||!captured_||invoke_failed_||!field_)
            throw std::logic_error("Field receipt requires one completed authenticated source/field capture");
        return field_->receipt;
    }
    /** Serialize the original owning field's discrete proof scalars only.
     * Workflow: require the actual known proof scope/status -> copy its RMS,
     * total-volume enclosure and conditional/error scalars -> restore precision.
     * RMS(Phi)=sqrt(sum(V_i*Phi_i^2)/sum(V_i)); weighted L2 is not exported or
     * obtained by an uncertified volume conversion. Marginal construction and
     * evaluation bounds are not additive: the complete A/B-correlated floor
     * remains conditional_residual.complete_residual_error_upper. No cellwise
     * certificate is attached to the separate long-double Green row reducer.
     */
    static void write_native_discrete_certificate(std::ostream& out,
        const Physical::Gravity::NativeRzFieldInspection& receipt) {
        using arch::elliptic::BoundaryErrorStatus;
        using arch::elliptic::BoundaryResidualStatus;
        using arch::elliptic::ResidualErrorComposition;
        const auto& rms=receipt.native_potential_rms;
        const auto& measure=receipt.native_measure;
        const auto& residual=receipt.conditional_residual;
        const auto& errors=receipt.residual_error;
        if(receipt.residual_norm_scope!=arch::elliptic::BoundaryResidualNormScope::RootDyadicRzWeights
            ||rms.status!=BoundaryErrorStatus::Bounded||measure.status!=BoundaryErrorStatus::Bounded
            ||residual.status!=BoundaryResidualStatus::Accepted
            ||!receipt.source_generation||!receipt.field_generation)
            throw std::logic_error("Actual field has no accepted native discrete certificate to serialize");
        for(double value:{rms.lower,rms.upper,measure.total_volume_lower,measure.total_volume_upper,
                residual.complete_residual_error_upper,residual.tolerance_safe,residual.rhs_norm_lower,
                residual.rhs_norm_upper,residual.residual_norm_upper,residual.rhs_error_upper,
                residual.total_residual_upper,errors.source_stored_upper,errors.rhs_assembly_stored_upper,
                errors.residual_arithmetic_stored_upper,errors.boundary_construction_native_upper,
                errors.boundary_potential_native_upper,errors.operator_construction_native_upper,
                errors.residual_evaluation_native_upper,errors.complete_native_upper}) {
            finite(value);
            if(value<0.)throw std::domain_error("Negative native discrete certificate scalar");
        }
        if(rms.lower>rms.upper||!(measure.total_volume_lower>0.)
            ||measure.total_volume_lower>measure.total_volume_upper
            ||residual.rhs_norm_lower>residual.rhs_norm_upper)
            throw std::domain_error("Reversed or nonpositive native discrete certificate enclosure");
        const char* composition=nullptr;
        switch(residual.error_composition) {
        case ResidualErrorComposition::SeparateRhsAndOperator:composition="SeparateRhsAndOperator";break;
        case ResidualErrorComposition::CorrelatedPrescribedBoundary:composition="CorrelatedPrescribedBoundary";break;
        default:throw std::logic_error("Actual field residual composition is unknown");
        }
        // Existing number() is exact-roundtrip binary64 and rejects NaN/Inf.
        // Green's later long-double evidence keeps its original stream precision.
        struct RestorePrecision {
            std::ostream& stream;std::streamsize prior;
            ~RestorePrecision() {stream.precision(prior);}
        } restore{out,out.precision()};
        out<<"{\"schema\":\"arch-private-native-discrete-field-certificate-1\",\"scope\":\"ideal-root-dyadic-native-discrete-operator\",\"physical_qualified\":false,\"continuous_potential_error_certified\":false,\"per_cell_residual_error_exported\":false,\"norm_scope\":\"RootDyadicRzWeights\",\"norm_kind\":\"volume-normalized-RMS\",\"weighted_L2_exported\":false,\"potential_semantics\":\"actual-stored-cell-center-point-Phi\",\"residual_vector_binding\":\"original-certified-operator-residual;not-long-double-row-diagnostic\",\"source_generation\":"
            <<receipt.source_generation<<",\"field_generation\":"<<receipt.field_generation;
        out<<",\"native_potential_rms\":{\"status\":\"Bounded\",\"units\":\"cm^2/s^2\",\"lower\":";
        number(out,rms.lower);out<<",\"upper\":";number(out,rms.upper);out<<'}';
        out<<",\"native_measure\":{\"status\":\"Bounded\",\"units\":\"cm^3\",\"total_volume_lower\":";
        number(out,measure.total_volume_lower);out<<",\"total_volume_upper\":";
        number(out,measure.total_volume_upper);out<<'}';
        out<<",\"conditional_residual\":{\"status\":\"Accepted\",\"units\":\"s^-2\",\"error_composition\":";
        text(out,composition);out<<",\"complete_residual_error_upper\":";number(out,residual.complete_residual_error_upper);
        out<<",\"tolerance_safe\":";number(out,residual.tolerance_safe);
        out<<",\"rhs_norm_lower\":";number(out,residual.rhs_norm_lower);
        out<<",\"rhs_norm_upper\":";number(out,residual.rhs_norm_upper);
        out<<",\"residual_norm_upper\":";number(out,residual.residual_norm_upper);
        out<<",\"rhs_error_upper\":";number(out,residual.rhs_error_upper);
        out<<",\"total_residual_upper\":";number(out,residual.total_residual_upper);out<<'}';
        out<<",\"marginal_error_scalars\":{\"units\":\"s^-2\",\"additive_floor\":false,\"stored_norm_scope\":\"StoredNativeWeights\",\"native_norm_scope\":\"RootDyadicRzWeights\",\"authoritative_complete_floor\":\"conditional_residual.complete_residual_error_upper\",\"source_stored_upper\":";
        number(out,errors.source_stored_upper);out<<",\"rhs_assembly_stored_upper\":";
        number(out,errors.rhs_assembly_stored_upper);out<<",\"residual_arithmetic_stored_upper\":";
        number(out,errors.residual_arithmetic_stored_upper);out<<",\"boundary_construction_native_upper\":";
        number(out,errors.boundary_construction_native_upper);out<<",\"boundary_potential_native_upper\":";
        number(out,errors.boundary_potential_native_upper);out<<",\"operator_construction_native_upper\":";
        number(out,errors.operator_construction_native_upper);out<<",\"residual_evaluation_native_upper\":";
        number(out,errors.residual_evaluation_native_upper);out<<",\"complete_native_upper\":";
        number(out,errors.complete_native_upper);out<<"}}";
    }
    /** Finite compatible source JSON after actual call exit. Failed/incomplete
     * captures cannot serialize as an accepted source. Outer run/ELF/fixture
     * hashing and actual file writes remain the separate runner's ownership.
     */
    std::string json() const {
        if(capturing_||(!checked_&&!published_source_checked())||!captured_)
            throw std::logic_error("Unverified source record cannot be exported");
        const auto& c=*captured_;std::ostringstream out;out.imbue(std::locale::classic());
        const auto& source_faces=published_source_checked()?owning_->field().faces:c.faces;
        if(published_source_checked())
            out<<"{\"schema\":\"arch-materialized-native-source-2\",\"scope\":\"published_native_source_and_field\",\"inspection_origin\":\"issued-owning-solution-snapshot\",\"published_source_checked\":true,\"source_only_checked\":false,\"physical_qualified\":false,\"source\":{\"sourceId\":";
        else
            out<<"{\"schema\":\"arch-materialized-native-source-1\",\"source_only_checked\":true,\"physical_qualified\":false,\"scope\":\"materialized_source_only\",\"source\":{\"sourceId\":";
        text(out,source_id());out<<",\"density_semantics\":\"actual-native-V-mean-piecewise-constant-full-ring\",\"leaves\":[";
        for(std::size_t n=0;n<c.leaves.size();++n) {
            if(n)out<<',';const auto& l=c.leaves[n];out<<"{\"id\":";text(out,leaf_id(l));
            out<<",\"r_lower\":";number(out,l.rl);out<<",\"r_upper\":";number(out,l.rh);
            out<<",\"z_lower\":";number(out,l.zl);out<<",\"z_upper\":";number(out,l.zh);
            out<<",\"density\":";number(out,l.rho);out<<",\"source_index\":"<<l.source_index
                <<",\"binding_block_index\":"<<l.block<<",\"source_offset\":"<<l.offset
                <<",\"level\":"<<l.level<<",\"logical_index\":";integers(out,l.logical);
            out<<",\"center\":";doubles(out,l.center);out<<",\"stored_operator_volume\":";number(out,l.volume);out<<'}';
        }
        out<<"]},\"root_bounds\":";doubles(out,c.root);
        out<<",\"source_identity\":{\"topology\":"<<c.identity.topology.value
            <<",\"operator_revision\":"<<c.identity.operator_revision
            <<",\"boundary_revision\":"<<c.identity.boundary_revision
            <<",\"accuracy_revision\":"<<c.identity.accuracy_revision<<",\"generation\":"<<c.generation
            <<",\"time\":";number(out,c.identity.input_time);out<<",\"G\":";number(out,c.identity.gravitational_constant);
        out<<",\"purpose\":";write_purpose(out,c.purpose);
        out<<",\"runtime_lease_generation\":"<<c.runtime_lease_generation
            <<",\"runtime_lease_authenticated\":"<<(c.runtime_lease_generation?"true":"false")
            <<",\"runtime_authority_scope\":";
        text(out,published_source_checked()?published_authority_scope(owning_->field()):
            c.runtime_lease_generation?"checked-source-materialization-only":"none-mathematical-tag-only");
        out<<",\"inputs\":[";for(std::size_t n=0;n<c.identity.inputs.size();++n){if(n)out<<',';input(out,c.identity.inputs[n]);}out<<"]}";
        out<<",\"service_configuration\":{\"origin\":\"actual-SelfGravity-constructor-copy\",\"type\":";text(out,c.service.type);
        out<<",\"boundary\":";text(out,c.service.boundary);out<<",\"g_x\":";number(out,c.service.g_x);
        out<<",\"g_y\":";number(out,c.service.g_y);out<<",\"g_z\":";number(out,c.service.g_z);
        out<<",\"relative_tolerance\":";number(out,c.service.relative_tolerance);
        out<<",\"absolute_tolerance\":";number(out,c.service.absolute_tolerance);
        out<<",\"max_cycles\":"<<c.service.max_cycles<<",\"derived_cache_choices\":null}";
        out<<",\"native_binding\":{\"dimension\":"<<c.mesh.dimension<<",\"root_cells\":";integers(out,c.mesh.cells);
        out<<",\"origin\":";doubles(out,c.mesh.origin);out<<",\"root_upper\":";doubles(out,c.mesh.root_upper);
        out<<",\"stored_nominal_spacing\":";doubles(out,c.mesh.spacing);out<<",\"periodic\":[";
        for(std::size_t n=0;n<3;++n){if(n)out<<',';out<<(c.periodic[n]?"true":"false");}out<<"],\"patches\":[";
        for(std::size_t n=0;n<c.patches.size();++n) {
            if(n)out<<',';const auto& p=c.patches[n];const auto& g=p.geometry;const auto& d=g.dyadic_identity;
            out<<"{\"uid\":"<<p.handle.uid.value<<",\"epoch\":"<<p.handle.epoch.value
                <<",\"field_memory\":"<<int(p.memory)<<",\"layout\":{\"dimension\":"<<p.layout.dimension
                <<",\"extent\":";integers(out,p.layout.extent);out<<",\"stride\":";integers(out,p.layout.stride);
            out<<",\"active_begin\":";integers(out,p.layout.active_begin);out<<",\"active_end\":";integers(out,p.layout.active_end);
            out<<",\"centering\":"<<int(p.layout.centering)<<"},\"bound_root_identity\":{\"bound\":"<<(d.bound?"true":"false")
                <<",\"root_lower\":";doubles(out,d.root_lower);out<<",\"root_upper\":";doubles(out,d.root_upper);
            out<<",\"root_blocks\":";integers(out,d.root_blocks);out<<",\"level\":"<<d.level<<",\"logical_block\":";integers(out,d.logical);
            out<<",\"periodic_axial\":"<<(d.periodic_axial?"true":"false")<<"},\"native_layout\":{\"dimension\":"<<g.dim
                <<",\"ng\":"<<g.ng<<",\"stride_y\":"<<g.stride_y<<",\"stride_z\":"<<g.stride_z<<",\"total_size\":"<<g.total_size
                <<",\"origin\":";doubles(out,std::array<double,3>{g.x1_min,g.x2_min,g.x3_min});
            out<<",\"actual_block_upper\":";doubles(out,g.actual_block_upper);out<<"}}";
        }
        out<<"]},\"observers\":[";bool first=true;
        for(const auto& l:c.leaves) {
            if(!first)out<<',';first=false;out<<"{\"id\":";text(out,"cell-center:"+leaf_id(l));
            out<<",\"kind\":\"cell-center\",\"source_index\":"<<l.source_index
                <<",\"r_observer\":";number(out,l.center[0]);out<<",\"z_observer\":";number(out,l.center[1]);out<<'}';
        }
        for(std::size_t n=0;n<source_faces.size();++n) {
            if(!first)out<<',';first=false;const auto& f=source_faces[n];out<<"{\"id\":";text(out,"actual-face:"+std::to_string(n));
            out<<",\"kind\":\"face-fragment-center\",\"face_index\":"<<n
                <<",\"left\":"<<f.left<<",\"right\":"<<f.right<<",\"axis\":"<<f.axis<<",\"boundary_side\":"<<f.boundary_side
                <<",\"native_bounds\":"<<(f.native_bounds?"true":"false")<<",\"area\":";number(out,f.area);
            out<<",\"r_observer\":";number(out,f.center[0]);out<<",\"z_observer\":";number(out,f.center[1]);
            out<<",\"center\":";doubles(out,f.center);out<<",\"fragment_lower\":";doubles(out,f.fragment_lower);
            out<<",\"fragment_upper\":";doubles(out,f.fragment_upper);out<<'}';
        }
        out<<']';
        if(!published_source_checked()) {
            out<<",\"field_call\":{\"invoke_failed\":"<<(invoke_failed_?"true":"false")
                <<",\"field_solve_failed\":"<<(invoke_failed_?"true":"false")<<",\"exception_type_actual\":";
            if(invoke_failed_)text(out,error_type_);else out<<"null";out<<",\"exception_message\":";
            if(invoke_failed_)text(out,error_message_);else out<<"null";out<<",\"actual_candidate_observed\":"<<(field_?"true":"false")<<'}';
        }
        out<<",\"candidate_field\":";
        if(!field_&&!published_source_checked())out<<"null";
        else {
            const auto& receipt=native_field_receipt();
            out<<"{\"physical_qualified\":false,\"sampling_semantics\":\"actual-native-candidate-cell-and-face-arrays\",\"field_generation\":"
                <<receipt.field_generation<<",\"source_generation\":"<<receipt.source_generation
                <<",\"purpose\":";write_purpose(out,receipt.purpose);
            out<<",\"runtime_lease_generation\":"<<receipt.runtime_lease_generation
                <<",\"runtime_lease_authenticated\":"<<(receipt.runtime_lease_authenticated?"true":"false")
                <<",\"runtime_authority_scope\":";
            text(out,published_source_checked()?published_authority_scope(receipt):"accepted-current-numerical-field-only");
            out
                <<",\"conditional_status\":"<<int(receipt.conditional_residual.status)
                <<",\"physical_status\":"<<int(*receipt.physical_status)<<",\"native_discrete_certificate\":";
            write_native_discrete_certificate(out,receipt);out<<",\"cell_values\":[";
            for(std::size_t n=0;n<receipt.potential.size();++n) {
                if(n)out<<',';out<<"{\"source_index\":"<<n<<",\"leaf_id\":";text(out,leaf_id(c.leaves[n]));
                out<<",\"potential\":";number(out,receipt.potential[n]);out<<",\"acceleration\":";
                doubles(out,std::array<double,3>{receipt.acceleration[0][n],receipt.acceleration[1][n],receipt.acceleration[2][n]});out<<'}';
            }
            out<<"],\"side_acceleration_layout\":\"cell-major: 6*cell+2*axis+side; side0=low, side1=high\",\"side_acceleration\":";
            values(out,receipt.side_acceleration);
            out<<",\"face_gradient_semantics\":\"increasing-coordinate derivative; force=-gradient\",\"face_gradients\":";
            values(out,receipt.face_gradient);out<<",\"face_values\":[";
            for(std::size_t n=0;n<receipt.faces.size();++n) {
                if(n)out<<',';const auto& f=receipt.faces[n];out<<"{\"face_index\":"<<n
                    <<",\"left\":"<<f.left<<",\"right\":"<<f.right<<",\"axis\":"<<f.axis
                    <<",\"boundary_side\":"<<f.boundary_side<<",\"construction\":"<<int(f.construction)
                    <<",\"native_bounds\":"<<(f.native_bounds?"true":"false")<<",\"area\":";number(out,f.area);
                out<<",\"center\":";doubles(out,f.center);out<<",\"fragment_lower\":";doubles(out,f.fragment_lower);
                out<<",\"fragment_upper\":";doubles(out,f.fragment_upper);out<<",\"fragment_width\":";doubles(out,f.fragment_width);
                out<<",\"gradient\":";number(out,receipt.face_gradient[n]);
                out<<",\"boundary_datum\":";number(out,receipt.boundary_values[n]);
                out<<",\"gradient_samples\":";indices(out,f.samples);out<<",\"gradient_coefficients\":";values(out,f.coefficients);
                out<<",\"boundary_coefficient\":";number(out,f.boundary_coefficient);out<<",\"anchor_coefficient\":";number(out,f.anchor_coefficient);
                out<<",\"value_samples\":";indices(out,f.value_samples);out<<",\"value_coefficients\":";values(out,f.value_coefficients);
                out<<",\"value_boundary_coefficient\":";number(out,f.value_boundary_coefficient);out<<'}';
            }
            out<<"]}";
        }
        out<<'}';return out.str();
    }
};
} // namespace arch::test
