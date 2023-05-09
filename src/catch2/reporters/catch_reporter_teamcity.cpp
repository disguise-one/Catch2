
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0
#include <catch2/reporters/catch_reporter_teamcity.hpp>

#include <catch2/reporters/catch_reporter_helpers.hpp>
#include <catch2/internal/catch_string_manip.hpp>
#include <catch2/internal/catch_enforce.hpp>
#include <catch2/internal/catch_textflow.hpp>
#include <catch2/catch_test_case_info.hpp>

#include <cassert>
#include <ostream>

namespace Catch {

    namespace {

        std::string fileNameTag(std::vector<Tag> const& tags) {
            auto it = std::find_if(begin(tags),
                                   end(tags),
                                   [] (Tag const& tag) {
                                       return tag.original.size() > 0
                                           && tag.original[0] == '#'; });
            if (it != tags.end()) {
                return static_cast<std::string>(
                    it->original.substr(1, it->original.size() - 1)
                );
            }
            return std::string();
        }

        static void normalizeNamespaceMarkers(std::string& str) {
            std::size_t pos = str.find( "::" );
            while ( pos != str.npos ) {
                str.replace( pos, 2, "." );
                pos += 1;
                pos = str.find( "::", pos );
            }
        }
        
        // if string has a : in first line will set indent to follow it on
        // subsequent lines
        void printHeaderString(std::ostream& os, std::string const& _string, std::size_t indent = 0) {
            std::size_t i = _string.find(": ");
            if (i != std::string::npos)
                i += 2;
            else
                i = 0;
            os << TextFlow::Column(_string)
                  .indent(indent + i)
                  .initialIndent(indent) << '\n';
        }

        std::string escape(StringRef str) {
            std::string escaped = static_cast<std::string>(str);
            replaceInPlace(escaped, "|", "||");
            replaceInPlace(escaped, "'", "`");
            replaceInPlace(escaped, "\n", "|n");
            replaceInPlace(escaped, "\r", "|r");
            replaceInPlace(escaped, "[", "|[");
            replaceInPlace(escaped, "]", "|]");
            return escaped;
        }
    } // end anonymous namespace


    TeamCityReporter::~TeamCityReporter() {}

    void TeamCityReporter::testRunStarting( TestRunInfo const& runInfo ) {
        m_stream << "##teamcity[testSuiteStarted name='" << escape( runInfo.name )
               << "']\n";
    }

    void TeamCityReporter::testRunEnded( TestRunStats const& runStats ) {
        m_stream << "##teamcity[testSuiteFinished name='"
               << escape( runStats.runInfo.name ) << "']\n";
    }

    void TeamCityReporter::assertionEnded(AssertionStats const& assertionStats) {
       
        AssertionResult const& result = assertionStats.assertionResult;
        
        if ( !result.isOk() ||
             result.getResultType() == ResultWas::ExplicitSkip ) {

            ReusableStringStream msg;
            if (!m_headerPrintedForThisSection)
                printSectionHeader(msg.get());
            m_headerPrintedForThisSection = true;

            msg << result.getSourceInfo() << '\n';

            switch (result.getResultType()) {
            case ResultWas::ExpressionFailed:
                msg << "expression failed";
                break;
            case ResultWas::ThrewException:
                msg << "unexpected exception";
                break;
            case ResultWas::FatalErrorCondition:
                msg << "fatal error condition";
                break;
            case ResultWas::DidntThrowException:
                msg << "no exception was thrown where one was expected";
                break;
            case ResultWas::ExplicitFailure:
                msg << "explicit failure";
                break;
            case ResultWas::ExplicitSkip:
                msg << "explicit skip";
                break;

                // We shouldn't get here because of the isOk() test
            case ResultWas::Ok:
            case ResultWas::Info:
            case ResultWas::Warning:
                CATCH_ERROR("Internal error in TeamCity reporter");
                // These cases are here to prevent compiler warnings
            case ResultWas::Unknown:
            case ResultWas::FailureBit:
            case ResultWas::Exception:
                CATCH_ERROR("Not implemented");
            }
            if (assertionStats.infoMessages.size() == 1)
                msg << " with message:";
            if (assertionStats.infoMessages.size() > 1)
                msg << " with messages:";
            for (auto const& messageInfo : assertionStats.infoMessages)
                msg << "\n  \"" << messageInfo.message << '"';

            std::string failedDetail = "";
            if (result.hasExpression()) {
                failedDetail = result.getExpressionInMacro() + "\n" + result.getExpandedExpression();
            }
 
            m_stream << "result failed:"<< escape( msg.str() ) <<"\n";
            std::string flowId = m_sectionNameStack.empty()? m_lastTestCaseFullName : m_sectionNameStack.top();

            if ( result.getResultType() == ResultWas::ExplicitSkip ) {
                m_stream << "ResultWas::ExplicitSkip\n";

                m_stream << "##teamcity[testIgnored";

                m_stream << " name='" << escape( currentTestCaseInfo->name ) << "'"
                     << " message='" << escape( msg.str() ) << "' flowId='" << flowId << "']\n";

            } else if ( currentTestCaseInfo->okToFail() ) {
                m_stream << "ResultWas::okToFail\n";
                msg << "- failure ignore as test marked as 'ok to fail'\n";
                m_stream << "##teamcity[testIgnored";

                m_stream << " name='" << escape( currentTestCaseInfo->name ) << "'"
                     << " message='" << escape( msg.str() ) << "' flowId='" << flowId << "']\n";
            } else {
                m_stream << "ResultWas::testFailed\n";
                m_stream << "##teamcity[testFailed";

                m_stream << " name='" << escape( currentTestCaseInfo->name ) << "'"
                     << " message='" << escape( msg.str() ) <<"'" << " details='"<<
                     escape(failedDetail) << "'" << " flowId='" << flowId << "']\n";
            }
            
            
        }
        m_stream.flush();
        
    }

    void TeamCityReporter::testCaseStarting(TestCaseInfo const& testInfo) {
        
        StreamingReporterBase::testCaseStarting(testInfo);
        m_testTimer.start();
        std::string testcaseName = escape(testInfo.name);
        
        m_lastTestCaseName = testcaseName;
        m_lastTestCaseFullName = testcaseName;
        
        
        std::string className = static_cast<std::string>(testInfo.className);
      
        //remove all spaces
        std::string::iterator end_pos = std::remove(className.begin(), className.end(), ' ');
        className.erase(end_pos, className.end());

        //This is replicating Junit naming convention
        if( className.empty() ) {
            className = fileNameTag(testInfo.tags);
            if ( className.empty() ) {
                className = "global.";
            }
        }
        if ( !m_config->name().empty() )
            className = static_cast<std::string>(m_config->name())+": "+static_cast<std::string>(m_config->name()) + '.' + className;
        normalizeNamespaceMarkers(className);

        m_lastTestCaseFullName = escape(className + testcaseName);
        
        m_stream << "testCaseStarting:" << testcaseName << " FullName:" << m_lastTestCaseFullName << "\n";
        
        m_stream << "##teamcity[testStarted name='" << m_lastTestCaseFullName << "' " << "flowId='"<< m_lastTestCaseFullName << "' ]\n";
        m_stream.flush();

    }

    void TeamCityReporter::testCaseEnded(TestCaseStats const& testCaseStats) {
        
        StreamingReporterBase::testCaseEnded(testCaseStats);
        
        auto const& testCaseInfo = *testCaseStats.testInfo;
        std::string testcaseName = escape(testCaseInfo.name);
        
        m_stream << "testCaseEnded:"<<testcaseName << " FullName:" << m_lastTestCaseFullName << "\n";

        if (!testCaseStats.stdOut.empty())
            m_stream << "##teamcity[testStdOut name='"
            << m_lastTestCaseFullName
            << "' out='" << escape(testCaseStats.stdOut) << "' " << "flowId='"<< m_lastTestCaseFullName <<"']\n";

        if (!testCaseStats.stdErr.empty())
            m_stream << "##teamcity[testStdErr name='"
            << m_lastTestCaseFullName
            << "' out='" << escape(testCaseStats.stdErr) << "' " << "flowId='"<< m_lastTestCaseFullName <<"']\n";
            

        m_stream << "##teamcity[testFinished name='"
            << m_lastTestCaseFullName << "' duration='"
            << m_testTimer.getElapsedMilliseconds() << "' " << "flowId='"<< m_lastTestCaseFullName <<"']\n";
        
        m_stream.flush();
        
    }

    void TeamCityReporter::sectionStarting(SectionInfo const& sectionInfo) {
        if(m_lastTestCaseName == sectionInfo.name) // this is a TEST_CASE(), not SECTION()
        {
            return;
        }

        Timer newTimer;
        newTimer.start();
        m_timerStack.push(newTimer);
        m_headerPrintedForThisSection = true;
        StreamingReporterBase::sectionStarting( sectionInfo );

        std::string testname = escape(sectionInfo.name);
           
        if(m_sectionNameStack.size()>0)
        {
                //this is a SECTION() or Nested SECTION()
            testname = escape(m_sectionNameStack.top() + "/" + sectionInfo.name);
        }
        else
        {
            //first SECTION() after TEST_CASE()
            testname = escape(m_lastTestCaseFullName + "/" + sectionInfo.name);
        }

        m_sectionNameStack.push(testname);

        m_stream << "sectionStarting:"<<testname<< "\n";
        m_stream << "##teamcity[testStarted name='" << testname << "' " << "flowId='"<< testname <<"']\n";
        
        m_stream.flush();

        
        

    }

    void TeamCityReporter::sectionEnded( SectionStats const& sectionStats ) {
        if(m_lastTestCaseName == sectionStats.sectionInfo.name)
        {
            return;
        }

        StreamingReporterBase::sectionEnded(sectionStats);
        
        Timer newTimer = m_timerStack.top();
        
        if(m_sectionNameStack.empty())
        {
            m_stream << "something wrong!!!\n";
            m_stream.flush();
            return;
        }

        std::string testname = escape(m_sectionNameStack.top());


        m_stream << "sectionEnded:"<<testname<< "\n";

        m_stream << "##teamcity[testFinished name='"
            << testname << "' duration='"
            << newTimer.getElapsedMilliseconds() << "' " << "flowId='"<< testname <<"']\n";

    
        m_stream.flush();
        m_timerStack.pop();
        m_sectionNameStack.pop();

    }

    void TeamCityReporter::printSectionHeader(std::ostream& os) {
        assert(!m_sectionStack.empty());

        if (m_sectionStack.size() > 1) {
            os << lineOfChars('-') << '\n';

            std::vector<SectionInfo>::const_iterator
                it = m_sectionStack.begin() + 1, // Skip first section (test case)
                itEnd = m_sectionStack.end();
            for (; it != itEnd; ++it)
                printHeaderString(os, it->name);
            os << lineOfChars('-') << '\n';
        }

        SourceLineInfo lineInfo = m_sectionStack.front().lineInfo;

        os << lineInfo << '\n';
        os << lineOfChars('.') << "\n\n";
    }

} // end namespace Catch
