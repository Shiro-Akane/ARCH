/**
 * @file ConfigurationRecord.h
 * @brief Decode exact bounded provenance without constructing scientific state.
 * Workflow:
 * 1. Consume explicit name/type/value lengths and reject duplicate fields.
 * 2. Preserve binary64 bit patterns, rejecting malformed/nonfinite encodings.
 * 3. Require the frozen final-configuration fields before publication.
 */
#pragma once

#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace arch::config {
using IdentityFields=std::map<std::string,std::pair<char,std::string>>;
/** Decode exact field bytes; this is an identity decoder, not an input/default parser. */
inline IdentityFields decode_identity_record(std::string_view text)
{
    if(text.empty()||text.size()>1024*1024||text.find(char(0))!=std::string_view::npos)
        throw std::invalid_argument("Invalid identity record length/content");
    std::size_t offset=0;
    const auto length=[&]() {
        const auto end=text.find(':',offset);
        if(end==std::string_view::npos||end==offset||end-offset>8)
            throw std::invalid_argument("Invalid identity record length");
        const auto token=text.substr(offset,end-offset);std::size_t value=0;
        const auto result=std::from_chars(token.data(),token.data()+token.size(),value);
        if(result.ec!=std::errc{}||result.ptr!=token.data()+token.size()||(token.size()>1&&token.front()=='0'))
            throw std::invalid_argument("Invalid identity record length");
        offset=end+1;return value;
    };
    IdentityFields fields;
    while(offset<text.size()) {
        const auto key_size=length();
        if(!key_size||key_size>text.size()-offset)throw std::invalid_argument("Invalid identity field name");
        std::string key(text.substr(offset,key_size));offset+=key_size;
        if(offset>=text.size())throw std::invalid_argument("Missing identity field type");
        const char type=text[offset++];const auto size=length();
        if(size>text.size()-offset)throw std::invalid_argument("Invalid identity value length");
        std::string value(text.substr(offset,size));offset+=size;
        if(offset>=text.size()||text[offset++]!='\n'||fields.contains(key))
            throw std::invalid_argument("Malformed or duplicate identity field");
        if(type=='d') {
            std::uint64_t bits=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),bits,16);
            if(value.size()!=16||value.find_first_not_of("0123456789abcdef")!=std::string::npos||parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()
                ||!std::isfinite(std::bit_cast<double>(bits)))throw std::invalid_argument("Invalid identity binary64");
        } else if(type=='i') {
            std::int64_t number=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
            if(value.empty()||parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||std::to_string(number)!=value)
                throw std::invalid_argument("Invalid identity integer");
        } else if(type=='b') {
            if(value!="0"&&value!="1")throw std::invalid_argument("Invalid identity boolean");
        } else if(type=='n') {
            if(!value.empty())throw std::invalid_argument("Invalid absent identity value");
        } else if(type!='s')throw std::invalid_argument("Unknown identity type");
        fields.emplace(std::move(key),std::pair{type,std::move(value)});
    }
    return fields;
}
/** Require a field's exact type and return its uninterpreted canonical value. */
inline const std::string& identity_value(const IdentityFields& fields,const std::string& name,char type)
{
    const auto found=fields.find(name);
    if(found==fields.end()||found->second.first!=type)throw std::invalid_argument("Missing/mistyped identity field: "+name);
    return found->second.second;
}
/** A digest cannot make an incomplete record into full effective configuration. */
inline void require_configuration_identity_coverage(const IdentityFields& fields)
{
#define ARCH_CONFIGURATION_IDENTITY_FIELD(NAME,TYPE) (void)identity_value(fields,NAME,TYPE);
#include "core/config/ConfigurationIdentityFields.inc"
#undef ARCH_CONFIGURATION_IDENTITY_FIELD
    for(const auto* name:{"version","case.id","case.compiled-source","resolved-execution.version","backend",
        "geometry-semantics","flux","reconstruction","limiter","time","eos","network","ode","linear","diffusion","boundary-identity"})
        (void)identity_value(fields,name,'s');
    for(const auto* name:{"amr.refine_species_names","io.plot_species_names"}) {
        const auto& count=identity_value(fields,std::string(name)+".count",'i');
        std::int64_t n=0;std::from_chars(count.data(),count.data()+count.size(),n);
        if(n<0||n>128)throw std::invalid_argument("Invalid identity selection length");
        for(std::int64_t i=0;i<n;++i)(void)identity_value(fields,std::string(name)+"."+std::to_string(i),'s');
    }
}
} // namespace arch::config
