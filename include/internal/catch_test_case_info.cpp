/*
 *  Created by Phil on 14/08/2012.
 *  Copyright 2012 Two Blue Cubes Ltd. All rights reserved.
 *
 *  Distributed under the Boost Software License, Version 1.0. (See accompanying
 *  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */

#include "catch_test_case_info.h"
#include "catch_enforce.h"
#include "catch_test_spec.h"
#include "catch_interfaces_testcase.h"
#include "catch_string_manip.h"

#include <cctype>
#include <exception>
#include <algorithm>
#include <sstream>

namespace Catch {

    namespace {
        using TCP_underlying_type = uint8_t;
        static_assert(sizeof(TestCaseProperties) == sizeof(TCP_underlying_type),
                      "The size of the TestCaseProperties is different from the assumed size");

        TestCaseProperties operator|(TestCaseProperties lhs, TestCaseProperties rhs) {
            return static_cast<TestCaseProperties>(
                static_cast<TCP_underlying_type>(lhs) | static_cast<TCP_underlying_type>(rhs)
            );
        }

        TestCaseProperties& operator|=(TestCaseProperties& lhs, TestCaseProperties rhs) {
            lhs = static_cast<TestCaseProperties>(
                static_cast<TCP_underlying_type>(lhs) | static_cast<TCP_underlying_type>(rhs)
            );
            return lhs;
        }

        TestCaseProperties operator&(TestCaseProperties lhs, TestCaseProperties rhs) {
            return static_cast<TestCaseProperties>(
                static_cast<TCP_underlying_type>(lhs) & static_cast<TCP_underlying_type>(rhs)
            );
        }

        bool applies(TestCaseProperties tcp) {
            static_assert(static_cast<TCP_underlying_type>(TestCaseProperties::None) == 0,
                          "TestCaseProperties::None must be equal to 0");
            return tcp != TestCaseProperties::None;
        }

        TestCaseProperties parseSpecialTag( StringRef tag ) {
            if( (!tag.empty() && tag[0] == '.')
               || tag == "!hide"_sr )
                return TestCaseProperties::IsHidden;
            else if( tag == "!throws"_sr )
                return TestCaseProperties::Throws;
            else if( tag == "!shouldfail"_sr )
                return TestCaseProperties::ShouldFail;
            else if( tag == "!mayfail"_sr )
                return TestCaseProperties::MayFail;
            else if( tag == "!nonportable"_sr )
                return TestCaseProperties::NonPortable;
            else if( tag == "!benchmark"_sr )
                return static_cast<TestCaseProperties>(TestCaseProperties::Benchmark | TestCaseProperties::IsHidden );
            else
                return TestCaseProperties::None;
        }
        bool isReservedTag( std::string const& tag ) {
            return parseSpecialTag( tag ) == TestCaseProperties::None
                && tag.size() > 0
                && !std::isalnum( static_cast<unsigned char>(tag[0]) );
        }
        void enforceNotReservedTag( std::string const& tag, SourceLineInfo const& _lineInfo ) {
            CATCH_ENFORCE( !isReservedTag(tag),
                          "Tag name: [" << tag << "] is not allowed.\n"
                          << "Tag names starting with non alphanumeric characters are reserved\n"
                          << _lineInfo );
        }

        std::string makeDefaultName() {
            static size_t counter = 0;
            return "Anonymous test case " + std::to_string(++counter);
        }

        StringRef extractFilenamePart(StringRef filename) {
            size_t lastDot = filename.size();
            while (lastDot > 0 && filename[lastDot - 1] != '.') {
                --lastDot;
            }
            --lastDot;

            size_t nameStart = lastDot;
            while (nameStart > 0 && filename[nameStart - 1] != '/' && filename[nameStart - 1] != '\\') {
                --nameStart;
            }

            return filename.substr(nameStart, lastDot - nameStart);
        }

        // Returns the upper bound on size of extra tags ([#file]+[.])
        size_t sizeOfExtraTags(StringRef filepath) {
            // [.] is 3, [#] is another 3
            const size_t extras = 3 + 3;
            return extractFilenamePart(filepath).size() + extras;
        }
    }

    std::unique_ptr<TestCaseInfo>
        makeTestCaseInfo(std::string const& _className,
                         NameAndTags const& nameAndTags,
                         SourceLineInfo const& _lineInfo ) {
        return std::make_unique<TestCaseInfo>(_className, nameAndTags, _lineInfo);
    }

    void setTags( TestCaseInfo& testCaseInfo, std::vector<std::string> tags ) {
        std::sort(begin(tags), end(tags));
        tags.erase(std::unique(begin(tags), end(tags)), end(tags));
        testCaseInfo.lcaseTags.clear();

        for( auto const& tag : tags ) {
            std::string lcaseTag = toLower( tag );
            testCaseInfo.properties = static_cast<TestCaseProperties>( testCaseInfo.properties | parseSpecialTag( lcaseTag ) );
            testCaseInfo.lcaseTags.push_back( lcaseTag );
        }
        testCaseInfo.tags = std::move(tags);
    }

    TestCaseInfo::TestCaseInfo(std::string const& _className,
                               NameAndTags const& _nameAndTags,
                               SourceLineInfo const& _lineInfo):
        name( _nameAndTags.name.empty() ? makeDefaultName() : _nameAndTags.name ),
        className( _className ),
        lineInfo( _lineInfo )
    {
        StringRef tags = _nameAndTags.tags;
        // We need to reserve enough space to store all of the tags
        // (including optional hidden tag and filename tag)
        auto requiredSize = tags.size() + sizeOfExtraTags(_lineInfo.file);
        backingTags.reserve(requiredSize);
        backingLCaseTags.reserve(requiredSize);

        // todo: copy tag helper to make copying tags to backing cleaner
        // todo: handle toLower?

        // We cannot copy the tags directly, as we need to normalize
        // some tags, so that [.foo] is copied as [.][foo].
        size_t tagStart = 0;
        size_t tagEnd = 0;
        bool inTag = false;

        for (size_t idx = 0; idx < tags.size(); ++idx) {
            auto c = tags[idx];
            if (c == '[') {
                assert(!inTag);
                inTag = true;
                tagStart = idx;
            }
            if (c == ']') {
                assert(inTag);
                inTag = false;
                tagEnd = idx;
                assert(tagStart < tagEnd);
                // Take the tag and handle it
                StringRef tagStr = tags.substr(tagStart+1, tagEnd - tagStart - 1);
                properties |= parseSpecialTag(tagStr);
                // TODO: if starts with [.], we need to copy differently
                backingTags += tagStr;
                backingLCaseTags += tagStr;
            }

        }
        // TODO: copy

        bool isHidden = false;

        // Parse out tags
        std::string tag;
        bool inTag = false;
        for (char c : nameAndTags.tags) {
            if (!inTag) {
                if (c == '[') {
                    inTag = true;
                }
            } else {
                if (c == ']') {
                    TestCaseProperties prop = parseSpecialTag(tag);
                    if (applies(prop & TestCaseProperties::IsHidden))
                        isHidden = true;
                    else if (prop == TestCaseProperties::None)
                        enforceNotReservedTag(tag, _lineInfo);

                    // Merged hide tags like `[.approvals]` should be added as
                    // `[.][approvals]`. The `[.]` is added at later point, so
                    // we only strip the prefix
                    if (startsWith(tag, '.') && tag.size() > 1) {
                        tag.erase(0, 1);
                    }
                    tags.push_back(tag);
                    tag.clear();
                    inTag = false;
                } else
                    tag += c;
            }
        }
        if (isHidden) {
            tags.push_back(".");
        }

        return std::make_unique<TestCaseInfo>(static_cast<std::string>(nameAndTags.name),
                                              _className, tags, _lineInfo);


        setTags( *this, _tags );
    }

    bool TestCaseInfo::isHidden() const {
        return applies( properties & TestCaseProperties::IsHidden );
    }
    bool TestCaseInfo::throws() const {
        return applies( properties & TestCaseProperties::Throws );
    }
    bool TestCaseInfo::okToFail() const {
        return applies( properties & (TestCaseProperties::ShouldFail | TestCaseProperties::MayFail ) );
    }
    bool TestCaseInfo::expectedToFail() const {
        return applies( properties & (TestCaseProperties::ShouldFail) );
    }

    void TestCaseInfo::addFilenameTag() {
        // empty for now
    }

    std::string TestCaseInfo::tagsAsString() const {
        std::string ret;
        // '[' and ']' per tag
        std::size_t full_size = 2 * tags.size();
        for (const auto& tag : tags) {
            full_size += tag.size();
        }
        ret.reserve(full_size);
        for (const auto& tag : tags) {
            ret.push_back('[');
            ret.append(tag);
            ret.push_back(']');
        }

        return ret;
    }


    bool TestCaseHandle::operator == ( TestCaseHandle const& rhs ) const {
        return m_invoker == rhs.m_invoker
            && m_info->name == rhs.m_info->name
            && m_info->className == rhs.m_info->className;
    }

    bool TestCaseHandle::operator < ( TestCaseHandle const& rhs ) const {
        return m_info->name < rhs.m_info->name;
    }

    TestCaseInfo const& TestCaseHandle::getTestCaseInfo() const {
        return *m_info;
    }

} // end namespace Catch
