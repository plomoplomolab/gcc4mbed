/* Copyright (C) 2017  Adam Green (https://github.com/adamgreen)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
// This program reads the external/mbed-os/targets/targets.json to produce the build/*-device.mk makefiles to
// allow GCC4MBED to build for any target device marked by the mbed team as supporting the GCC_ARM toolchain.
#include <assert.h>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jsmn/jsmn.h"


// MRI library to be used for specific targets.
const char* g_mriLibs[][2] =
{
    { "LPC1768",       "$(GCC4MBED_DIR)/mri/libmri_mbed1768.a" },
    { "LPC4330_M4",    "$(GCC4MBED_DIR)/mri/libmri_bambino210.a" },
    { "NUCLEO_F429ZI", "$(GCC4MBED_DIR)/mri/libmri_stm32f429-disco.a" }
};


static void displayUsage()
{
    printf("Usage: mbedUpdater [-v]\n"
           "Where:\n"
           "    -v enables verbose logging.\n");
}


// Macro to only log information when verbose logging is enabled.
bool    g_verboseEnabled = false;

#define VERBOSE_LOG(...) if (g_verboseEnabled) printf(__VA_ARGS__)


enum ArmCoreType
{
    CORE_UNKOWN,
    ARM7TDMI_S,
    Cortex_M0,
    Cortex_M0PLUS,
    Cortex_M1,
    Cortex_M3,
    Cortex_M4,
    Cortex_M4F,
    Cortex_M7,
    Cortex_M7F,
    Cortex_M7FD,
    Cortex_A9
};

// Valid bits for TargetObject::m_releaseVersionsBitmask field.
#define MBED_2 (1 << 0)
#define MBED_5 (1 << 1)

// Valid bits for TargetObject::m_validFieldsBitmask.
#define CORE_VALID                   (1 << 0)
#define SUPPORTED_TOOLCHAINS_VALID   (1 << 1)
#define DEVICE_HAS_VALID             (1 << 2)
#define SUPPORTED_FORM_FACTORS_VALID (1 << 3)
#define RELEASE_VERSIONS_VALID       (1 << 5)
#define EXTRA_LABELS_VALID           (1 << 6)
#define MACROS_VALID                 (1 << 7)
#define FEATURES_VALID               (1 << 8)



class SizedString
{
public:
    SizedString(const char* pString, int stringLength)
    {
        set(pString, stringLength);
    }
    SizedString()
    {
        set(NULL, 0);
    }

    void set(const char* pString, int stringLength)
    {
        m_pVal = pString;
        m_length = stringLength;
    }

    void setName(const SizedString& name)
    {
        *this = name;
    }

    int compare(const SizedString& other)
    {
        int minLength = (m_length < other.m_length) ? m_length : other.m_length;
        int compResult = strncmp(m_pVal, other.m_pVal, minLength);

        if (compResult == 0)
        {
            if (m_length == other.m_length)
                return 0;
            else if (m_length < other.m_length)
                return -1;
            else
                return 1;
        }
        return compResult;
    }

    int compare(const char* pOther)
    {
        return compare(SizedString(pOther, strlen(pOther)));
    }

    int compareName(const SizedString& other)
    {
        return compare(other);
    }

    bool isEmpty()
    {
        return m_pVal == NULL;
    }

    void print()
    {
        printf("%.*s", m_length, m_pVal);
    }
    
    const char* m_pVal;
    int         m_length;
};


template <class T, bool IGNORE_DUPLICATES = false, int GROWTH_AMOUNT = 16>
class SortedArray
{
public:
    SortedArray()
    {
        m_pObjects = NULL;
        m_usedCount = 0;
        m_allocCount = 0;
    }
    ~SortedArray()
    {
        dealloc();
    }

    void dealloc()
    {
        if (!m_pObjects)
            return;
            
        for (int i = 0 ; i < m_allocCount ; i++)
            m_pObjects[i].~T();
        free(m_pObjects);

        m_pObjects = NULL;
        m_usedCount = 0;
        m_allocCount = 0;
    }

    bool alloc(int itemCount)
    {
        dealloc();

        m_pObjects = (T*)malloc(sizeof(T) * itemCount);
        if (!m_pObjects)
            return false;
        for (int i = 0 ; i < itemCount ; i++)
            new(m_pObjects + i) T();
        
        m_allocCount = itemCount;
        m_usedCount = 0;

        return true;
    }

    bool grow(int addItems)
    {
        int newCount = m_allocCount + addItems;
        T* pRealloc = (T*)realloc(m_pObjects, sizeof(T) * newCount);
        if (!pRealloc)
            return false;

        m_pObjects = pRealloc;
        for (int i = m_allocCount ; i < newCount ; i++)
            new(m_pObjects + i) T();
        m_allocCount = newCount;

        return true;
    }
    
    int length() const
    {
        return m_usedCount;
    }

    T& operator[](int element)
    {
        assert ( element >= 0 && element < m_usedCount );
        return m_pObjects[element];
    }

    const T& operator[](int element) const
    {
        assert ( element >= 0 && element < m_usedCount );
        return m_pObjects[element];
    }

    bool addElementByName(const SizedString& name, T** ppAdd)
    {
        if (m_usedCount >= m_allocCount)
        {
            bool growResult = grow(GROWTH_AMOUNT);
            if (!growResult)
                return false;
        }
        
        int findResult = findNameOrGreater(name);
        if (findResult >= m_usedCount)
        {
            // This item is last in sort order so simply append to end.
            assert ( findResult == m_usedCount );
            m_pObjects[m_usedCount].setName(name);
            *ppAdd = &m_pObjects[m_usedCount];
            m_usedCount++;
            return true;
        }

        int compResult = m_pObjects[findResult].compareName(name);
        if (compResult == 0)
        {
            // This target name is already in the list.
            return IGNORE_DUPLICATES ? true : false;
        }
        assert ( compResult > 0 );

        // Insert item into middle of array.
        int itemsToShift = m_usedCount - findResult;
        memmove(&m_pObjects[findResult+1], &m_pObjects[findResult], sizeof(T) * itemsToShift);
        new(m_pObjects + findResult) T();
        m_pObjects[findResult].setName(name);
        *ppAdd = &m_pObjects[findResult];
        m_usedCount++;

        return true;
    }

    bool findElementByName(const SizedString& name, T** ppFound)
    {
        int findResult = findNameOrGreater(name);
        int compResult = m_pObjects[findResult].compareName(name);
        if (compResult == 0)
        {
            *ppFound = &m_pObjects[findResult];
            return true;
        }
        return false;
    }

protected:
    int findNameOrGreater(const SizedString& name)
    {
        int high = m_usedCount - 1;
        int low = 0;
        int mid = 0;

        if (m_usedCount == 0)
            return 0;

        while (low <= high)
        {
            mid = (low + high) / 2;

            int compResult = m_pObjects[mid].compareName(name);
            if (compResult == 0)
            {
                return mid;
            }
            else if (compResult > 0)
            {
                high = mid - 1;
            }
            else
            {
                low = mid + 1;
            }
        }

        int greater = mid;
        if (m_pObjects[greater].compareName(name) < 0)
        {
            greater++;
            assert ( greater == m_usedCount || m_pObjects[greater].compareName(name) > 0 );
        }
        return greater;
    }
    
    T*      m_pObjects;
    int     m_usedCount;
    int     m_allocCount;
};


class SortedSizedStringArray : public SortedArray<SizedString, true>
{
public:
    bool populate(const char* pRaw, jsmntok_t* pArrayToken, int* pTokensUsed)
    {
        jsmntok_t* pCurr = pArrayToken;
        *pTokensUsed = 0;
        assert ( pArrayToken->type == JSMN_ARRAY );

        // Array token itself knows the number of elements in the array so process it first.
        int elementCount = pCurr->size;
        bool result = alloc(elementCount);
        if (!result)
        {
            fprintf(stderr, "error: Failed to allocate memory for string array.\n");
            return false;
        }
        pCurr++;

        // Add string elements to array.
        for (int i = 0 ; i < elementCount ; i++)
        {
            if (pCurr->type != JSMN_STRING)
            {
                fprintf(stderr, "error: Expected all array elements to be strings.\n");
                *pTokensUsed = i + 1;
                return false;
            }
            SizedString* pString = NULL;
            SizedString name = SizedString(pRaw + pCurr->start, pCurr->end - pCurr->start);
            result = addElementByName(name, &pString);
            if (!result)
            {
                fprintf(stderr, "error: Failed to add %.*s to string array.\n", name.m_length, name.m_pVal);
                *pTokensUsed = i + 1;
                return false;
            }

            pCurr++;
        }

        *pTokensUsed = 1 + elementCount;
        return true;
    }

    bool insert(const SizedString& name)
    {
        SizedString* pString = NULL;
        bool result = addElementByName(name, &pString);
        if (!result)
        {
            fprintf(stderr, "error: Failed to add %.*s to string array.\n", name.m_length, name.m_pVal);
            return false;
        }
        return true;
    }

    bool insert(const char* pName)
    {
        return insert(SizedString(pName, strlen(pName)));
    }

    bool insert(const SortedSizedStringArray& array)
    {
        for (int i = 0 ; i < array.length() ; i++)
        {
            bool result = insert(array[i]);
            if (!result)
                return false;
        }
        return true;
    }

    void print()
    {
        printf("[");
        for (int i = 0 ; i < m_usedCount ; i++)
        {
            if (i != 0)
                printf(", ");
            printf("\"");
            m_pObjects[i].print();
            printf("\"");
        }
        printf("]");
    }
};


class SizedStringArray
{
public:
    SizedStringArray()
    {
        m_pObjects = NULL;
        m_count = 0;
    }
    ~SizedStringArray()
    {
        dealloc();
    }

    void dealloc()
    {
        delete[] m_pObjects;
        m_pObjects = NULL;
        m_count = 0;
    }

    bool alloc(int itemCount)
    {
        dealloc();

        m_pObjects = new SizedString[itemCount];
        if (!m_pObjects)
            return false;
        m_count = itemCount;

        return true;
    }

    int length() const
    {
        return m_count;
    }

    SizedString& operator[](int element)
    {
        assert ( element >= 0 && element < m_count );
        return m_pObjects[element];
    }

    const SizedString& operator[](int element) const
    {
        assert ( element >= 0 && element < m_count );
        return m_pObjects[element];
    }

protected:
    SizedString*    m_pObjects;
    int             m_count;
};


class TargetObject;


class Targets : public SortedArray<TargetObject>
{
public:
    Targets()
    {
        m_failedFinalizations = 0;
    }

    void startFinalizationPass()
    {
        m_failedFinalizations = 0;
    }

    void encounteredFinalizationFailure()
    {
        m_failedFinalizations++;
    }

    bool hasFailedFinalization()
    {
        return m_failedFinalizations > 0;
    }
    

protected:
    uint32_t m_failedFinalizations;
};


class TargetObject
{
public:
    TargetObject()
    {
        m_core = CORE_UNKOWN;
        m_releaseVersionsBitmask = 0;
        m_parentCount = 0;
        m_ppParents = NULL;
        m_validFieldsBitmask = 0;
        m_isGccSupported = false;
        m_isPublic = true;
        m_hasCompletedFinalization = false;
    }

    ~TargetObject()
    {
        free(m_ppParents);
        m_ppParents = NULL;
    }
    
    void setName(const SizedString& name)
    {
        m_name = name;
    }

    int compareName(const SizedString& other)
    {
        return m_name.compare(other);
    }

    void print()
    {
        m_name.print();
        printf("\n");
        
        printf("    inherits = ");
        m_inherits.print();
        printf("\n");

        printf("    public = %s\n", m_isPublic ? "true" : "false");

        if (m_validFieldsBitmask & SUPPORTED_TOOLCHAINS_VALID)
        {
            printf("    isGccSupported = %s\n", m_isGccSupported ? "true" : "false");
        }
        if (m_validFieldsBitmask & RELEASE_VERSIONS_VALID)
        {
            printf("    release_versions = ");
            if (m_releaseVersionsBitmask & MBED_2)
                printf("MBED_2 ");
            if (m_releaseVersionsBitmask & MBED_5)
                printf("MBED_5 ");
            printf("\n");
        }
        if (m_validFieldsBitmask & CORE_VALID)
        {
            const char* coreTypes[] =
            {
                "Unknown",
                "ARM7TDMI_S",
                "Cortex_M0",
                "Cortex_M0PLUS",
                "Cortex_M1",
                "Cortex_M3",
                "Cortex_M4",
                "Cortex_M7",
                "Cortex_M4F",
                "Cortex_M7F",
                "Cortex_M7FD",
                "Cortex_A9"
            };
            printf("    core = %s\n", coreTypes[m_core]);
        }

        if (m_validFieldsBitmask & EXTRA_LABELS_VALID)
        {
            printf("    extra_labels = ");
            m_extraLabels.print();
            printf("\n");
        }
        printf("    extra_labels_add = ");
        m_extraLabelsAdd.print();
        printf("\n");
        printf("    targets = ");
        m_targets.print();
        printf("\n");
        printf("    labels = ");
        m_labels.print();
        printf("\n");

        if (m_validFieldsBitmask & DEVICE_HAS_VALID)
        {
            printf("    device_has = ");
            m_deviceHas.print();
            printf("\n");
        }
        if (m_validFieldsBitmask & SUPPORTED_FORM_FACTORS_VALID)
        {
            printf("    supported_form_factors = ");
            m_supportedFormFactors.print();
            printf("\n");
        }
        if (m_validFieldsBitmask & MACROS_VALID)
        {
            printf("    macros = ");
            m_macros.print();
            printf("\n");
        }
        printf("    macros_add = ");
        m_macrosAdd.print();
        printf("\n");
        if (m_validFieldsBitmask & FEATURES_VALID)
        {
            printf("    features = ");
            m_features.print();
            printf("\n");
        }
        printf("    features_add = ");
        m_featuresAdd.print();
        printf("\n");
        if (!m_hasCompletedFinalization)
        {
            printf("    hasCompletedFinalizations = false\n");
        }
    }

    bool finalize(Targets* pTargets)
    {
        if (m_hasCompletedFinalization)
            return true;
            
        if (!findParents(pTargets))
        {
            if (!m_ppParents)
            {
                // Failed memory allocation so should be tracked as an error.
                return false;
            }
            else
            {
                // Ignore parent not found errors here since we will need to re-attempt finalization later.
                return true;
            }
        }

        if (!finalizeCore())
            return false;
        if (!finalizeSupportedToolchains())
            return false;
        if (!finalizeReleaseVersions())
            return false;
        if (!finalizeLabels())
            return false;
        if (!finalizeDeviceHas())
            return false;
        if (!finalizeSupportedFormFactors())
            return false;
        if (!finalizeMacros())
            return false;
        if (!finalizeFeatures())
            return false;

        m_hasCompletedFinalization = true;
        return true;
    }

    bool findParents(Targets* pTargets)
    {
        if (!m_ppParents)
        {
            int parentCount = m_inherits.length();
            m_ppParents = (TargetObject**) malloc(sizeof(m_ppParents[0]) * parentCount);
            if (!m_ppParents)
            {
                fprintf(stderr, "error: Failed to allocate array for parent pointers.\n");
                return false;
            }
            m_parentCount = parentCount;
        }

        for (int i = 0 ; i < m_parentCount ; i++)
        {
            TargetObject* p = NULL;
            SizedString name = m_inherits[i];
            bool result = pTargets->findElementByName(name, &p);
            if (!result || !p->m_hasCompletedFinalization)
            {
                pTargets->encounteredFinalizationFailure();
                VERBOSE_LOG("verbose: Failed to find \"%.*s\" parent target. Will retry.\n", name.m_length, name.m_pVal);
                return false;
            }
            m_ppParents[i] = p;
        }

        return true;
    }

    bool finalizeCore()
    {
        TargetObject* pObjectWithValidField = findValidFieldInHierarchy(CORE_VALID);
        if (pObjectWithValidField == NULL)
            return true;

        m_core = pObjectWithValidField->m_core;
        m_validFieldsBitmask |= CORE_VALID;
        return true;
    }

    TargetObject* findValidFieldInHierarchy(uint32_t validFieldToSearch)
    {
        if (m_validFieldsBitmask & validFieldToSearch)
        {
            return this;
        }

        for (int i = 0 ; i < m_parentCount ; i++)
        {
            if (m_ppParents[i]->m_validFieldsBitmask & validFieldToSearch)
                return m_ppParents[i];
        }

        return NULL;
    }

    bool finalizeSupportedToolchains()
    {
        TargetObject* pObjectWithValidField = findValidFieldInHierarchy(SUPPORTED_TOOLCHAINS_VALID);
        if (pObjectWithValidField == NULL)
            return true;

        m_isGccSupported = pObjectWithValidField->m_isGccSupported;
        m_validFieldsBitmask |= SUPPORTED_TOOLCHAINS_VALID;
        return true;
    }

    bool finalizeReleaseVersions()
    {
        TargetObject* pObjectWithValidField = findValidFieldInHierarchy(RELEASE_VERSIONS_VALID);
        if (pObjectWithValidField == NULL)
            return true;

        m_releaseVersionsBitmask = pObjectWithValidField->m_releaseVersionsBitmask;
        m_validFieldsBitmask |= RELEASE_VERSIONS_VALID;
        return true;
    }
    
    bool finalizeLabels()
    {
        if (!addTargetNameLabels())
            return false;
        if (!addExtraLabels())
            return false;
        if (!addCoreSpecificLabels())
            return false;
        return true;
    }

    bool addTargetNameLabels()
    {
        bool result = false;
        
        for (int i = 0 ; i < m_parentCount ; i++)
        {
            SizedString targetName = m_ppParents[i]->m_name;
            result = m_targets.insert(m_ppParents[i]->m_targets);
            if (!result)
            {
                fprintf(stderr, "error: Failed to insert \"%.*s\" targets into target list.\n",
                        targetName.m_length, targetName.m_pVal);
                return false;
            }
        }

        if (0 != m_name.compare("Target"))
        {
            result = m_targets.insert(m_name);
            if (!result)
            {
                fprintf(stderr, "error: Failed to insert \"%.*s\" into target list.\n",
                        m_name.m_length, m_name.m_pVal);
                return false;
            }
        }

        result = m_labels.insert(m_targets);
        if (!result)
        {
            fprintf(stderr, "error: Failed to insert target labels into label list.\n");
            return false;
        }

        return true;
    }

    bool addExtraLabels()
    {
        bool result = false;

        // Only inherit/merge from all parents if this object doesn't specifically have the field set.
        if (0 == (m_validFieldsBitmask & EXTRA_LABELS_VALID))
        {
            for (int i = 0 ; i < m_parentCount ; i++)
            {
                result = m_extraLabels.insert(m_ppParents[i]->m_extraLabels);
                if (!result)
                {
                    fprintf(stderr, "error: Failed to insert extra labels from \"%.*s\" into \"%.*s\".\n",
                            m_ppParents[i]->m_name.m_length, m_ppParents[i]->m_name.m_pVal,
                            m_name.m_length, m_name.m_pVal);
                    return false;
                }
            }
        }

        // Add in the values that this object wants to specifically add.
        result = m_extraLabels.insert(m_extraLabelsAdd);
        if (!result)
        {
            fprintf(stderr, "error: Failed to insert additional extra labels.\n");
            return false;
        }

        m_validFieldsBitmask |= EXTRA_LABELS_VALID;

        // Add the final list of extra_labels into the total label list.
        result = m_labels.insert(m_extraLabels);
        if (!result)
        {
            fprintf(stderr, "error: Failed to insert extra labels into label list.\n");
            return false;
        }

        return true;
    }

    bool addCoreSpecificLabels()
    {
        const size_t maxStrings = 4;
        const char*  coreSpecificLabels[][maxStrings] =
        {
            { NULL, NULL, NULL, NULL }, //CORE_UNKOWN
            { "ARM7", "LIKE_CORTEX_ARM7", NULL, NULL }, //ARM7TDMI_S,
            { "M0", "CORTEX_M", "LIKE_CORTEX_M0", NULL }, //Cortex_M0,
            { "M0P", "CORTEX_M", "LIKE_CORTEX_M0", NULL }, //Cortex_M0PLUS,
            { "M1", "CORTEX_M", "LIKE_CORTEX_M1", NULL }, //Cortex_M1,
            { "M3", "CORTEX_M", "LIKE_CORTEX_M3", NULL }, //Cortex_M3,
            { "M4", "CORTEX_M", "RTOS_M4_M7", "LIKE_CORTEX_M4" }, //Cortex_M4,
            { "M4", "CORTEX_M", "RTOS_M4_M7", "LIKE_CORTEX_M4" }, //Cortex_M4F,
            { "M7", "CORTEX_M", "RTOS_M4_M7", "LIKE_CORTEX_M7" }, //Cortex_M7,
            { "M7", "CORTEX_M", "RTOS_M4_M7", "LIKE_CORTEX_M7" }, //Cortex_M7F,
            { "M7", "CORTEX_M", "RTOS_M4_M7", "LIKE_CORTEX_M7" }, //Cortex_M7FD,
            { "A9", "CORTEX_A", "LIKE_CORTEX_A9", NULL } //Cortex_A9
        };

        for (size_t i = 0 ; i < maxStrings ; i++)
        {
            const char* pCoreLabel = coreSpecificLabels[m_core][i];
            if (!pCoreLabel)
                break;
                
            bool result = m_labels.insert(pCoreLabel);
            if (!result)
            {
                fprintf(stderr, "error: Failed to insert \"%s\" into label list.\n", pCoreLabel);
                return false;
            }
        }
        
        return true;
    }

    bool finalizeDeviceHas()
    {
        bool result = false;
        
        TargetObject* pObjectWithValidField = findValidFieldInHierarchy(DEVICE_HAS_VALID);
        if (pObjectWithValidField == NULL || pObjectWithValidField == this)
            return true;

        result = m_deviceHas.insert(pObjectWithValidField->m_deviceHas);
        if (!result)
        {
            fprintf(stderr, "error: Failed to inherit device_has fields.\n");
            return false;
        }

        m_validFieldsBitmask |= DEVICE_HAS_VALID;
        return true;
    }

    bool finalizeSupportedFormFactors()
    {
        bool result = false;
        
        TargetObject* pObjectWithValidField = findValidFieldInHierarchy(SUPPORTED_FORM_FACTORS_VALID);
        if (pObjectWithValidField == NULL || pObjectWithValidField == this)
            return true;

        result = m_supportedFormFactors.insert(pObjectWithValidField->m_supportedFormFactors);
        if (!result)
        {
            fprintf(stderr, "error: Failed to inherit supported_form_factors fields.\n");
            return false;
        }

        m_validFieldsBitmask |= SUPPORTED_FORM_FACTORS_VALID;
        return true;
    }

    bool finalizeMacros()
    {
        bool result = false;

        // Only inherit/merge from all parents if this object doesn't specifically have the field set.
        if (0 == (m_validFieldsBitmask & MACROS_VALID))
        {
            for (int i = 0 ; i < m_parentCount ; i++)
            {
                result = m_macros.insert(m_ppParents[i]->m_macros);
                if (!result)
                {
                    fprintf(stderr, "error: Failed to insert macros from \"%.*s\" into \"%.*s\".\n",
                            m_ppParents[i]->m_name.m_length, m_ppParents[i]->m_name.m_pVal,
                            m_name.m_length, m_name.m_pVal);
                    return false;
                }
            }
        }

        // Add in the values that this object wants to specifically add.
        result = m_macros.insert(m_macrosAdd);
        if (!result)
        {
            fprintf(stderr, "error: Failed to insert additional macro.\n");
            return false;
        }

        m_validFieldsBitmask |= MACROS_VALID;

        return true;
    }

    bool finalizeFeatures()
    {
        bool result = false;

        // Only inherit/merge from all parents if this object doesn't specifically have the field set.
        if (0 == (m_validFieldsBitmask & FEATURES_VALID))
        {
            for (int i = 0 ; i < m_parentCount ; i++)
            {
                result = m_features.insert(m_ppParents[i]->m_features);
                if (!result)
                {
                    fprintf(stderr, "error: Failed to insert features from \"%.*s\" into \"%.*s\".\n",
                            m_ppParents[i]->m_name.m_length, m_ppParents[i]->m_name.m_pVal,
                            m_name.m_length, m_name.m_pVal);
                    return false;
                }
            }
        }

        // Add in the values that this object wants to specifically add.
        result = m_features.insert(m_featuresAdd);
        if (!result)
        {
            fprintf(stderr, "error: Failed to insert additional features.\n");
            return false;
        }

        m_validFieldsBitmask |= FEATURES_VALID;

        return true;
    }

    bool produceTargetMakefile(const char* pOutputDirectory)
    {
        const size_t maxDefines = 6;
        const char* coreSpecificDefines[][maxDefines] =
        {
            { NULL, NULL, NULL, NULL, NULL, NULL }, //CORE_UNKOWN,
            { NULL, NULL, NULL, NULL, NULL, NULL }, //ARM7TDMI_S,
            { "__CORTEX_M0", "ARM_MATH_CM0", "__CMSIS_RTOS", "__MBED_CMSIS_RTOS_CM", NULL, NULL }, //Cortex_M0,
            { "__CORTEX_M0PLUS", "ARM_MATH_CM0PLUS", "__CMSIS_RTOS", "__MBED_CMSIS_RTOS_CM", NULL, NULL }, //Cortex_M0PLUS,
            { "__CORTEX_M3", "ARM_MATH_CM1", "__CMSIS_RTOS", "__MBED_CMSIS_RTOS_CM", NULL, NULL }, //Cortex_M1,
            { "__CORTEX_M3", "ARM_MATH_CM3", "__CMSIS_RTOS", "__MBED_CMSIS_RTOS_CM", NULL, NULL }, //Cortex_M3,
            { "__CORTEX_M4", "ARM_MATH_CM4", "__CMSIS_RTOS", "__MBED_CMSIS_RTOS_CM", NULL, NULL }, //Cortex_M4,
            { "__CORTEX_M4", "ARM_MATH_CM4", "__FPU_PRESENT=1", "__CMSIS_RTOS", "__MBED_CMSIS_RTOS_CM", NULL }, //Cortex_M4F,
            { "__CORTEX_M7", "ARM_MATH_CM7", "__CMSIS_RTOS", "__MBED_CMSIS_RTOS_CM", NULL, NULL }, //Cortex_M7,
            { "__CORTEX_M7", "ARM_MATH_CM7", "__FPU_PRESENT=1", "__CMSIS_RTOS", "__MBED_CMSIS_RTOS_CM", NULL }, //Cortex_M7F,
            { "__CORTEX_M7", "ARM_MATH_CM7", "__FPU_PRESENT=1", "__CMSIS_RTOS", "__MBED_CMSIS_RTOS_CM", NULL }, //Cortex_M7FD,
            { "__CORTEX_A9", "ARM_MATH_CA9", "__FPU_PRESENT", "__CMSIS_RTOS", "__EVAL", "__MBED_CMSIS_RTOS_CA9" } //Cortex_A9
        };
        const char* coreSpecificFlags[] =
        {
            "-mcpu=unknown", //CORE_UNKOWN,
            "-mcpu=arm7tdmi-s", //ARM7TDMI_S,
            "-mcpu=cortex-m0 -mthumb", //Cortex_M0,
            "-mcpu=cortex-m0plus -mthumb", //Cortex_M0PLUS,
            "-mcpu=cortex-m1 -mthumb", //Cortex_M1,
            "-mcpu=cortex-m3 -mthumb", //Cortex_M3,
            "-mcpu=cortex-m4 -mthumb", //Cortex_M4,
            "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=softfp", //Cortex_M4F,
            "-mcpu=cortex-m7 -mthumb", //Cortex_M7,
            "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=softfp", //Cortex_M7F,
            "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=softfp", //Cortex_M7FD,
            "-mcpu=cortex-a9 -marm -mthumb-interwork  -march=armv7-a -mfpu=vfpv3 -mfloat-abi=hard -mno-unaligned-access" //Cortex_A9
        };
        bool retVal = false;
        const char* pMriLib = "";
        
        // Only create makefiles for public targets that support GCC_ARM.
        if (!m_isPublic || !m_isGccSupported)
            return true;
            
        char filename[256];
        snprintf(filename, sizeof(filename), 
                 "%s/%.*s-device.mk",
                 pOutputDirectory,
                 m_name.m_length, m_name.m_pVal);

        VERBOSE_LOG("verbose: Creating %s...\n", filename);

        FILE* pFile = fopen(filename, "w");
        if (!pFile)
        {
            fprintf(stderr, "error: Failed to create %s\n", filename);
            perror(NULL);
            goto Error;
        }

        fprintf(pFile, "# This target makefile was automatically generated by mbedUpdater.\n\n");

        fprintf(pFile, "# Device for which the code should be built.\n");
        fprintf(pFile, "MBED_DEVICE        := %.*s\n\n", m_name.m_length, m_name.m_pVal);

        fprintf(pFile, "# Can skip parsing of this makefile if user hasn't requested this device.\n");
        fprintf(pFile, "ifeq \"$(findstring $(MBED_DEVICE),$(DEVICES))\" \"$(MBED_DEVICE)\"\n\n");

        fprintf(pFile, "# Compiler flags which are specifc to this device.\n");
        fprintf(pFile, "TARGETS_FOR_DEVICE := $(BUILD_TYPE_TARGET) TARGET_UVISOR_UNSUPPORTED");
        for (int i = 0 ; i < m_labels.length() ; i++)
        {
            SizedString label = m_labels[i];
            fprintf(pFile, " TARGET_%.*s", label.m_length, label.m_pVal);
        }
        fprintf(pFile, "\n");

        fprintf(pFile, "FEATURES_FOR_DEVICE :=");
        for (int i = 0 ; i < m_features.length() ; i++)
        {
            SizedString feature = m_features[i];
            fprintf(pFile, " FEATURE_%.*s", feature.m_length, feature.m_pVal);
        }
        fprintf(pFile, "\n");

        fprintf(pFile, "PERIPHERALS_FOR_DEVICE :=");
        for (int i = 0 ; i < m_deviceHas.length() ; i++)
        {
            SizedString deviceHas = m_deviceHas[i];
            fprintf(pFile, " DEVICE_%.*s", deviceHas.m_length, deviceHas.m_pVal);
        }
        fprintf(pFile, "\n");

        fprintf(pFile, "GCC_DEFINES := $(patsubst %%,-D%%,$(TARGETS_FOR_DEVICE))\n");
        fprintf(pFile, "GCC_DEFINES += $(patsubst %%,-D%%=1,$(FEATURES_FOR_DEVICE))\n");
        fprintf(pFile, "GCC_DEFINES += $(patsubst %%,-D%%=1,$(PERIPHERALS_FOR_DEVICE))\n");
        fprintf(pFile, "GCC_DEFINES +=");
        for (size_t i = 0 ; i < maxDefines ; i++)
        {
            const char* pDefine = coreSpecificDefines[m_core][i];
            if (!pDefine)
                break;
            fprintf(pFile, " -D%s", pDefine);
        }
        fprintf(pFile, "\n");
        if (m_macros.length() != 0)
        {
            fprintf(pFile, "GCC_DEFINES +=");
            for (int i = 0 ; i < m_macros.length() ; i++)
            {
                SizedString& macro = m_macros[i];
                fprintf(pFile, " -D%.*s", macro.m_length, macro.m_pVal);
            }
            fprintf(pFile, "\n");
        }
        for (int i = 0 ; i < m_configMacroNames.length() ; i++)
        {
            SizedString name = m_configMacroNames[i];
            SizedString value = m_configMacroValues[i];
            SizedString help = m_configMacroHelp[i];

            if (!name.isEmpty() && !value.isEmpty())
            {
                if (!help.isEmpty())
                {
                    fprintf(pFile, "\n# %.*s\n", help.m_length, help.m_pVal);
                }
                fprintf(pFile, "%.*s_%.*s ?= \"%.*s\"\n",
                        m_name.m_length, m_name.m_pVal,
                        name.m_length, name.m_pVal,
                        value.m_length, value.m_pVal);
                fprintf(pFile, "GCC_DEFINES += -D%.*s=$(%.*s_%.*s)\n",
                        name.m_length, name.m_pVal,
                        m_name.m_length, m_name.m_pVal,
                        name.m_length, name.m_pVal);
            }
        }
        fprintf(pFile, "\n");

        fprintf(pFile, "C_FLAGS   := %s\n", coreSpecificFlags[m_core]);
        fprintf(pFile, "ASM_FLAGS := %s\n", coreSpecificFlags[m_core]);
        fprintf(pFile, "LD_FLAGS  := %s\n\n", coreSpecificFlags[m_core]);

        fprintf(pFile, "# Extra platform specific object files to link into file binary.\n");
        fprintf(pFile, "DEVICE_OBJECTS := \n\n");

        fprintf(pFile, "# Version of MRI library to use for this device.\n");
        for (size_t i = 0 ; i < sizeof(g_mriLibs)/sizeof(g_mriLibs[0]) ; i++)
        {
            if (0 == m_name.compare(g_mriLibs[i][0]))
            {
                pMriLib = g_mriLibs[i][1];
                break;
            }
        }
        fprintf(pFile, "DEVICE_MRI_LIB := %s\n", pMriLib);
        fprintf(pFile, "\n");

        fprintf(pFile, "# Determine all mbed source folders which are a match for this device so that it only needs to be done once.\n");
        fprintf(pFile, "DEVICE_MBED_DIRS := $(call filter_dirs,$(RAW_MBED_DIRS),$(TARGETS_FOR_DEVICE),$(FEATURES_FOR_DEVICE))\n\n");

        fprintf(pFile, "# Linker script to be used.  Indicates what should be placed where in memory.\n");
        fprintf(pFile, "%.*s_LSCRIPT  ?= $(call find_target_linkscript,$(DEVICE_MBED_DIRS))\n", m_name.m_length, m_name.m_pVal);
        fprintf(pFile, "LSCRIPT := $(%.*s_LSCRIPT)\n\n", m_name.m_length, m_name.m_pVal);

        fprintf(pFile, "include $(GCC4MBED_DIR)/build/device-common.mk\n\n");


        fprintf(pFile, "else\n");
        fprintf(pFile, "# Have an empty rule for this device since it isn't supported.\n");
        fprintf(pFile, ".PHONY: $(MBED_DEVICE)\n\n");
        fprintf(pFile, "ifeq \"$(OS)\" \"Windows_NT\"\n");
        fprintf(pFile, "$(MBED_DEVICE):\n");
        fprintf(pFile, "\t@REM >nul\n");
        fprintf(pFile, "else\n");
        fprintf(pFile, "$(MBED_DEVICE):\n");
        fprintf(pFile, "\t@#\n");
        fprintf(pFile, "endif\n");
        fprintf(pFile, "endif # ifeq \"$(findstring $(MBED_DEVICE),$(DEVICES))\"...\n");
        
        retVal = true;
    Error:
        if (pFile)
        {
            fclose(pFile);
            pFile = NULL;
        }
        return retVal;
    }
    

    TargetObject**          m_ppParents;
    SortedSizedStringArray  m_inherits;
    SortedSizedStringArray  m_targets;
    SortedSizedStringArray  m_labels;
    SortedSizedStringArray  m_extraLabels;
    SortedSizedStringArray  m_extraLabelsAdd;
    SortedSizedStringArray  m_macros;
    SortedSizedStringArray  m_macrosAdd;
    SortedSizedStringArray  m_features;
    SortedSizedStringArray  m_featuresAdd;
    SortedSizedStringArray  m_deviceHas;
    SortedSizedStringArray  m_supportedFormFactors;
    SizedStringArray        m_configMacroNames;
    SizedStringArray        m_configMacroValues;
    SizedStringArray        m_configMacroHelp;
    SizedString             m_name;

    int                     m_parentCount;
    uint32_t                m_validFieldsBitmask;
    uint32_t                m_releaseVersionsBitmask;
    
    ArmCoreType             m_core;

    bool                    m_isGccSupported;
    bool                    m_isPublic;
    bool                    m_hasCompletedFinalization;
};


static bool parseCommandLine(int argc, const char** argv);
static char* getBasePath(const char* pExecutable);
static long getFileLength(FILE* pFile);
static bool parseTargetObjects(Targets* pTargets, const char* pJsonString, jsmntok_t* pJsonTokens, int jsonTokenCount);
static bool parseTargetObject(Targets* pTargets, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseTargetAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseCoreAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseInheritsAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseArrayAttribute(TargetObject* pTarget, 
                                const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd, 
                                SortedSizedStringArray* pArray, const char* pAttributeName, uint32_t validBit);
static bool parseSupportedToolchainsAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseDeviceHasAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseSupportedFormFactorsAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parsePublicAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseReleaseVersionsAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseExtraLabelsAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseExtraLabelsAddAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseMacrosAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseMacrosAddAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseFeaturesAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseFeaturesAddAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseConfigObject(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseConfigMacro(TargetObject* pTarget, int macroIndex, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool parseConfigMacroElement(TargetObject* pTarget, int macroIndex, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool ignoreValue(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool ignoreObject(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool ignoreAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool ignoreArray(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
static bool produceTargetMakefiles(const char* pOutputDirectory, Targets* pTargets);
static bool produceHelloWorldMakefile(const char* pFilename, Targets* pTargets, uint32_t releaseVersion);

int main(int argc, const char** argv)
{
    char        filename[] = "external/mbed-os/targets/targets.json";
    char        helloWorldMbed5Makefile[] = "samples/HelloWorld/TestPass5.mk";
    char        helloWorldMbed2Makefile[] = "samples/HelloWorld/TestPass2.mk";
    char        targetMakefileBasePath[] = "build";
    char*       pBasePath = NULL;
    size_t      basePathLength = 0;
    char*       pFilename = NULL;
    char*       pHelloWorldMbed5Makefile = NULL;
    char*       pHelloWorldMbed2Makefile = NULL;
    char*       pTargetMakefileBasePath = NULL;
    FILE*       pFile = NULL; 
    long        fileLength = -1;
    int         exitCode = 1;
    char*       pFileData = NULL;
    size_t      bytesRead = 0;
	jsmntok_t*  pJsonTokens = NULL;
	int         jsonTokenCount = 0;
	int         parseResult = -1;
	bool        result = false;
	jsmn_parser jsonParser;
    Targets     targets;

    if (!parseCommandLine(argc, argv))
    {
        displayUsage();
        goto Error;
    }
        
    // Calculate paths to where source and destination files should be located based on known location of
    // mbedUpdater executable in the gcc4mbed git repository.
    pBasePath = getBasePath(argv[0]);
    if (!pBasePath)
        goto Error;

    basePathLength = strlen(pBasePath);
    pFilename = (char*)malloc(basePathLength + 1 + sizeof(filename));
    pHelloWorldMbed5Makefile = (char*)malloc(basePathLength + 1 + sizeof(helloWorldMbed5Makefile));
    pHelloWorldMbed2Makefile = (char*)malloc(basePathLength + 1 + sizeof(helloWorldMbed2Makefile));
    pTargetMakefileBasePath = (char*)malloc(basePathLength + 1 + sizeof(targetMakefileBasePath));
    if (!pFilename || !pHelloWorldMbed5Makefile || !pHelloWorldMbed2Makefile || !pTargetMakefileBasePath)
    {
        fprintf(stderr, "error: Failed to allocate filename strings.\n");
        goto Error;
    }

    sprintf(pFilename, "%s/%s", pBasePath, filename);
    sprintf(pHelloWorldMbed5Makefile, "%s/%s", pBasePath, helloWorldMbed5Makefile);
    sprintf(pHelloWorldMbed2Makefile, "%s/%s", pBasePath, helloWorldMbed2Makefile);
    sprintf(pTargetMakefileBasePath, "%s/%s", pBasePath, targetMakefileBasePath);

    // Open targets.json file.
    pFile = fopen(pFilename, "r");
    if (!pFile)
    {
        fprintf(stderr, "error: Failed to open %s. - ", pFilename);
        perror(NULL);
        goto Error;
    }

    // Read all of targets.json file into memory.
    fileLength = getFileLength(pFile);
    if (fileLength < 0)
    {
        goto Error;
    }

    pFileData = (char*)malloc(fileLength);
    if (!pFileData)
    {
        fprintf(stderr, "error: Failed to allocate %ld bytes for reading %s.\n", fileLength, pFilename);
        goto Error;
    }

    bytesRead = fread(pFileData, 1, fileLength, pFile);
    if (bytesRead != (size_t)fileLength)
    {
        fprintf(stderr, "error: Failed to read %ld bytes from %s. - ", fileLength, pFilename);
        perror(NULL);
        goto Error;
    }
    
	// Run 1st pass of JSON parser to determine required token count.
	jsmn_init(&jsonParser);
	parseResult = jsmn_parse(&jsonParser, pFileData, fileLength, NULL, 0);
	if (parseResult < 0)
	{
	   fprintf(stderr, "error: Failed 1st JSON parsing pass of %s.\n", pFilename);
	   goto Error;
	}

    // Allocate required token count.
	jsonTokenCount = parseResult;
	pJsonTokens = (jsmntok_t*)malloc(sizeof(*pJsonTokens) * jsonTokenCount);
	if (!pJsonTokens)
	{
	   fprintf(stderr, "error: Failed to allocate %d JSON tokens.\n", jsonTokenCount);
	   goto Error;
	}

    // Run 2nd pass of JSON parser to actually parse out tokens.
	jsmn_init(&jsonParser);
	parseResult = jsmn_parse(&jsonParser, pFileData, fileLength, pJsonTokens, jsonTokenCount);
	if (parseResult < 0)
	{
	   fprintf(stderr, "error: Failed 2nd JSON parsing pass of %s.\n", pFilename);
	   goto Error;
	}

	result = parseTargetObjects(&targets, pFileData, pJsonTokens, jsonTokenCount);
	if (!result)
	{
	   fprintf(stderr, "error: Failed parsing mbed target objects in %s.\n", pFilename);
	   goto Error;
	}

	result = produceTargetMakefiles(pTargetMakefileBasePath, &targets);
	if (!result)
	{
	   fprintf(stderr, "error: Failed producing target makefiles.\n");
	   goto Error;
	}

	result = produceHelloWorldMakefile(pHelloWorldMbed5Makefile, &targets, MBED_5);
	if (!result)
	{
	   fprintf(stderr, "error: Failed producing %s.\n", pHelloWorldMbed5Makefile);
	   goto Error;
	}

	result = produceHelloWorldMakefile(pHelloWorldMbed2Makefile, &targets, MBED_2);
	if (!result)
	{
	   fprintf(stderr, "error: Failed producing %s.\n", pHelloWorldMbed2Makefile);
	   goto Error;
	}

    printf("Update successful.\n");
    
    exitCode = 0;
Error:
    if (pFileData)
        free(pFileData);
    if (pFile)
        fclose(pFile);

    free(pBasePath);
    free(pFilename);
    free(pHelloWorldMbed5Makefile);
    free(pHelloWorldMbed2Makefile);
    free(pTargetMakefileBasePath);
        
    return exitCode;
}

static bool parseCommandLine(int argc, const char** argv)
{
    bool result = true;
    
    for (int i = 1 ; i < argc ; i++)
    {
        if (0 == strcmp(argv[i], "-v"))
        {
            g_verboseEnabled = true;
        }
        else
        {
            fprintf(stderr, "error: \"%s\" is an unrecognized command line parameter.\n", argv[i]);
            result = false;
        }
    }

    return result;
}

static char* getBasePath(const char* pExecutable)
{
    char* pAbsolutePath = realpath(pExecutable, NULL);
    if (!pAbsolutePath)
    {
        fprintf(stderr, "error: Failed to calculate absolute path of %s.\n", pExecutable);
        perror(NULL);
        return NULL;
    }

    char* lastSlashes[2] = { NULL, NULL };
    char* pCurr = pAbsolutePath;
    while (*pCurr)
    {
        if (*pCurr == '/')
        {
            lastSlashes[0] = lastSlashes[1];
            lastSlashes[1] = pCurr;
        }
        pCurr++;
    }

    if (lastSlashes[0] == NULL)
    {
        fprintf(stderr, "error: Failed to find base path for executable in absolute path: %s\n", pAbsolutePath);
        free(pAbsolutePath);
        return NULL;
    }
    *lastSlashes[0] = '\0';

    return pAbsolutePath;
}

static long getFileLength(FILE* pFile)
{
    int result = fseek(pFile, 0, SEEK_END);
    if (result < 0)
    {
        fprintf(stderr, "error: fseek() failed - ");
        perror(NULL);
        return -1;
    }

    long length = ftell(pFile);
    if (length == -1)
    {
        fprintf(stderr, "error: ftell() failed - ");
        perror(NULL);
        return -1;
    }

    result = fseek(pFile, 0, SEEK_SET);
    if (result < 0)
    {
        fprintf(stderr, "error: fseek() failed - ");
        perror(NULL);
        return -1;
    }

    return length;
}

static bool parseTargetObjects(Targets* pTargets, const char* pRaw, jsmntok_t* pJsonTokens, int jsonTokenCount)
{
    jsmntok_t* pCurr = pJsonTokens;
    jsmntok_t* pEnd = pJsonTokens + jsonTokenCount;

    // JSON file should open with an unnamed root object.
    if (pCurr->type != JSMN_OBJECT)
    {
        fprintf(stderr, "error: Expected JSON root object\n");
        return false;
    }

    // Allocate enough space to track all of the target objects found in JSON file.
    int targetCount = pCurr->size;
    pCurr++;
    bool allocResult = pTargets->alloc(targetCount);
    if (!allocResult)
    {
        fprintf(stderr, "error: Failed to allocate target list for %d items.\n", targetCount);
        return false;
    }
    VERBOSE_LOG("verbose: Detected %d target objects.\n", targetCount);

    // Start parsing each of the target objects and adding them to the list of targets.
    for (int i = 0 ; i < targetCount ; i++)
    {
        bool result = parseTargetObject(pTargets, pRaw, &pCurr, pEnd);
        if (!result)
        {
            fprintf(stderr, "error: Failed to parse target object #%d\n", i+1);
            return false;
        }
    }

    // Some finalizations may have  failed because of failure to find parent target objects during first pass.
    while (pTargets->hasFailedFinalization())
    {
        pTargets->startFinalizationPass();
        for (int i = 0 ; i < pTargets->length() ; i++)
        {
            bool result = (*pTargets)[i].finalize(pTargets);
            if (!result)
            {
                fprintf(stderr, "error: Failed to post finalization step.\n");
                return false;
            }
        }
    }

    // Bail out early if we can skip verbose target object dumping.
    if (!g_verboseEnabled)
        return true;

    printf("\nverbose: Sorted list of target object details.\n");
    for (int i = 0 ; i < targetCount ; i++)
    {
        (*pTargets)[i].print();
    }

    return true;
}

static bool parseTargetObject(Targets* pTargets, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    jsmntok_t*    pCurr = *ppCurr;
    TargetObject* pTarget = NULL;
    SizedString   targetName;
    
    if (pCurr >= pEnd)
    {
        assert ( pCurr < pEnd );
        return false;
    }

    // Should be a named object.
    if (pCurr->type != JSMN_STRING)
    {
        fprintf(stderr, "error: Expected target object name.\n");
        return false;
    }
    targetName.set(pRaw + pCurr->start, pCurr->end - pCurr->start);
    pCurr++;

    // Should now find start of target object.
    if (pCurr->type != JSMN_OBJECT)
    {
        fprintf(stderr, "error: Expected target object for \"%.*s\".\n", targetName.m_length, targetName.m_pVal);
        return false;
    }
    int attributeCount = pCurr->size;
    pCurr++;
    VERBOSE_LOG("verbose: Parsing %d attributes for \"%.*s\"\n", attributeCount, targetName.m_length, targetName.m_pVal);

    // Find location in sorted target collection for this named target.
    bool result = pTargets->addElementByName(targetName, &pTarget);
    if (!result)
    {
        fprintf(stderr, "error: Failed to insert target \"%.*s\" in global list.\n", 
                targetName.m_length, targetName.m_pVal);
    }
    
    // Parse the target object's attributes.
    for (int i = 0 ; i < attributeCount ; i++)
    {
        result = parseTargetAttribute(pTarget, pRaw, &pCurr, pEnd);
        if (!result)
        {
            fprintf(stderr, "error: Failed to parse attribute #%d of \"%.*s\".\n",
                    i+1, targetName.m_length, targetName.m_pVal);
            return false;
        }
    }

    result = pTarget->finalize(pTargets);
    if (!result)
    {
        fprintf(stderr, "error: Failed to finalize \"%.*s\".\n",targetName.m_length, targetName.m_pVal);
        return false;
    }

    *ppCurr = pCurr;
    return true;
}

static bool parseTargetAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    jsmntok_t* pCurr = *ppCurr;
    const struct AttributeHandlerEntry
    {
        const char* pName;
        bool (*handler)(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd);
    } attributeTable[] =
    {
        { "core", parseCoreAttribute },
        { "inherits", parseInheritsAttribute },
        { "supported_toolchains", parseSupportedToolchainsAttribute },
        { "device_has", parseDeviceHasAttribute },
        { "supported_form_factors", parseSupportedFormFactorsAttribute },
        { "public", parsePublicAttribute },
        { "release_versions", parseReleaseVersionsAttribute },
        { "extra_labels", parseExtraLabelsAttribute },
        { "extra_labels_add", parseExtraLabelsAddAttribute },
        { "macros", parseMacrosAttribute },
        { "macros_add", parseMacrosAddAttribute },
        { "features", parseFeaturesAttribute },
        { "features_add", parseFeaturesAddAttribute },
        { "config", parseConfigObject },
        // The following attributes are knowingly ignored.
        { "default_toolchain", NULL },
        { "is_disk_virtual", NULL },
        { "detect_code", NULL },
        { "default_lib", NULL },
        { "device_name", NULL },
        // UNDONE: I am not completely sure about all of these yet.
        { "bootloader_supported", NULL },
        { "post_binary_hook", NULL },
        { "OUTPUT_EXT", NULL },
        { "progen", NULL },
        { "program_cycle_s", NULL },
        { "OVERRIDE_BOOTLOADER_FILENAME", NULL },
        { "MERGE_BOOTLOADER", NULL },
        { "MERGE_SOFT_DEVICE", NULL },
        { "EXPECTED_SOFTDEVICES_WITH_OFFSETS", NULL },
        { "overrides", NULL },
        { "forced_reset_timeout", NULL },
        { "target_overrides", NULL }
    };

    if (pCurr >= pEnd)
    {
        assert ( pCurr < pEnd );
        return false;
    }

    // Should be a named object.
    if (pCurr->type != JSMN_STRING)
    {
        fprintf(stderr, "error: Expected attribute name.\n");
        return false;
    }

    // Parse rest based on attribute name.
    SizedString name(pRaw + pCurr->start, pCurr->end - pCurr->start);
    pCurr++;
    for (size_t i = 0 ; i < sizeof(attributeTable)/sizeof(attributeTable[0]) ; i++)
    {
        if (0 == name.compare(attributeTable[i].pName))
        {
            bool result;
            if (!attributeTable[i].handler)
            {
                VERBOSE_LOG("verbose: Ignoring attribute \"%.*s\".\n", name.m_length, name.m_pVal);
                result = ignoreValue(pTarget, pRaw, &pCurr, pEnd);
            }
            else
            {
                result = attributeTable[i].handler(pTarget, pRaw, &pCurr, pEnd);
            }
            if (!result)
                return false;

            *ppCurr = pCurr;
            return true;
        }
    }

    // Handle cases where the attribute name was unknown.
    fprintf(stderr, "error: Unrecognized attribute \"%.*s\".\n", name.m_length, name.m_pVal);
    return false;
}

static bool parseCoreAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    struct CoreEntries
    {
        const char* pString;
        ArmCoreType type;
    } coreTable[] =
    {
        { "ARM7TDMI-S", ARM7TDMI_S },
        { "Cortex-M0",  Cortex_M0 },
        { "Cortex-M0+", Cortex_M0PLUS },
        { "Cortex-M1", Cortex_M1 },
        { "Cortex-M3", Cortex_M3 },
        { "Cortex-M4", Cortex_M4 },
        { "Cortex-M7", Cortex_M7 },
        { "Cortex-M4F", Cortex_M4F },
        { "Cortex-M7F", Cortex_M7F },
        { "Cortex-M7FD", Cortex_M7FD },
        { "Cortex-A9", Cortex_A9 }
    };
    jsmntok_t* pCurr = *ppCurr;

    if (pCurr >= pEnd)
    {
        assert ( pCurr < pEnd );
        return false;
    }

    // Could be a primitive null or string value.
    if (pCurr->type == JSMN_PRIMITIVE && pRaw[pCurr->start] == 'n')
    {
        // Was set to null which is default setting of m_core anyway.
        pCurr++;
    }
    else if (pCurr->type == JSMN_STRING)
    {
        bool        isGood = false;
        const char* p = pRaw + pCurr->start;
        int         length = pCurr->end - pCurr->start;
        pCurr++;

        for (size_t i = 0 ; i < sizeof(coreTable)/sizeof(coreTable[0]) ; i++)
        {
            if (0 == strncmp(p, coreTable[i].pString, length))
            {
                pTarget->m_core = coreTable[i].type;
                isGood = true;
                break;
            }
        }

        if (!isGood)
        {
            fprintf(stderr, "error: Unrecognized 'core' type \"%.*s\".\n", length, p);
            return false;
        }
    }
    else
    {
        fprintf(stderr, "error: Unrecognized 'core' value\n");
        return false;
    }


    pTarget->m_validFieldsBitmask |= CORE_VALID;
    *ppCurr = pCurr;
    return true;
}

static bool parseInheritsAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    return parseArrayAttribute(pTarget, pRaw, ppCurr, pEnd,
                               &pTarget->m_inherits, "inherits", 0);

}

static bool parseArrayAttribute(TargetObject* pTarget, 
                                const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd, 
                                SortedSizedStringArray* pArray, const char* pAttributeName, uint32_t validBit)
{
    jsmntok_t* pCurr = *ppCurr;

    if (pCurr >= pEnd)
    {
        assert ( pCurr < pEnd );
        return false;
    }

    // Value should be an array of strings.
    if (pCurr->type == JSMN_ARRAY)
    {
        int tokensUsed = 0;
        
        bool result = pArray->populate(pRaw, pCurr, &tokensUsed);
        if (!result)
        {
            fprintf(stderr, "error: Failed while parsing '%s' value.\n", pAttributeName);
            return false;
        }
        pCurr += tokensUsed;
    }
    else
    {
        fprintf(stderr, "error: Unrecognized '%s' value.\n", pAttributeName);
        return false;
    }

    pTarget->m_validFieldsBitmask |= validBit;
    *ppCurr = pCurr;
    return true;
}

static bool parseSupportedToolchainsAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    jsmntok_t* pCurr = *ppCurr;

    if (pCurr >= pEnd)
    {
        assert ( pCurr < pEnd );
        return false;
    }

    // Could be a primitive null or array of strings.
    if (pCurr->type == JSMN_PRIMITIVE && pRaw[pCurr->start] == 'n')
    {
        // Was set to null which means that GCC isn't supported.
        pCurr++;
    }
    else if (pCurr->type == JSMN_ARRAY)
    {
        // Process array of strings representing supported toolchains.
        int elementCount = pCurr->size;
        pCurr++;
        for (int i = 0 ; i < elementCount ; i++)
        {
            if (pCurr->type != JSMN_STRING)
            {
                fprintf(stderr, "error: Expected only string entries in 'supported_toolchains' value.\n");
                return false;
            }
    
            const char* p = pRaw + pCurr->start;
            int         length = pCurr->end - pCurr->start;
            if (0 == strncmp(p, "GCC_ARM", length))
            {
                pTarget->m_isGccSupported = true;
            }
    
            pCurr++;
        }
    }
    else
    {
        fprintf(stderr, "error: Expected only an array of strings as 'supported_toolchains' value.\n");
        return false;
    }

    pTarget->m_validFieldsBitmask |= SUPPORTED_TOOLCHAINS_VALID;
    *ppCurr = pCurr;
    return true;
}

static bool parseDeviceHasAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    return parseArrayAttribute(pTarget, pRaw, ppCurr, pEnd,
                               &pTarget->m_deviceHas, "device_has", DEVICE_HAS_VALID);
}

static bool parseSupportedFormFactorsAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    return parseArrayAttribute(pTarget, pRaw, ppCurr, pEnd,
                               &pTarget->m_supportedFormFactors, "supported_form_factors", SUPPORTED_FORM_FACTORS_VALID);
}

static bool parsePublicAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    jsmntok_t* pCurr = *ppCurr;

    if (pCurr >= pEnd)
    {
        assert ( pCurr < pEnd );
        return false;
    }

    // Could be a primitive null or array of strings.
    if (pCurr->type == JSMN_PRIMITIVE && pRaw[pCurr->start] == 't')
    {
        pTarget->m_isPublic = true;
        pCurr++;
    }
    else if (pCurr->type == JSMN_PRIMITIVE && pRaw[pCurr->start] == 'f')
    {
        pTarget->m_isPublic = false;
        pCurr++;
    }
    else
    {
        fprintf(stderr, "error: Expected only true or false for 'public' value.\n");
        return false;
    }

    *ppCurr = pCurr;
    return true;
}

static bool parseReleaseVersionsAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    jsmntok_t* pCurr = *ppCurr;

    if (pCurr >= pEnd)
    {
        assert ( pCurr < pEnd );
        return false;
    }

    // Could be a primitive null or string value.
    if (pCurr->type == JSMN_PRIMITIVE && pRaw[pCurr->start] == 'n')
    {
        // Was set to null which is default setting of m_core anyway.
        pCurr++;
    }
    else if (pCurr->type == JSMN_ARRAY)
    {
        // Process array of strings representing release versions.
        int elementCount = pCurr->size;
        pCurr++;
        for (int i = 0 ; i < elementCount ; i++)
        {
            if (pCurr->type != JSMN_STRING)
            {
                fprintf(stderr, "error: Expected only string entries in 'release_versions' value.\n");
                return false;
            }
    
            const char* p = pRaw + pCurr->start;
            int         length = pCurr->end - pCurr->start;
            if (0 == strncmp(p, "2", length))
            {
                pTarget->m_releaseVersionsBitmask |= MBED_2;
            }
            else if (0 == strncmp(p, "5", length))
            {
                pTarget->m_releaseVersionsBitmask |= MBED_5;
            }
            else
            {
                fprintf(stderr, "error: Unrecognized 'release_versions' type \"%.*s\".\n", length, p);
                return false;
            }
    
            pCurr++;
        }
    }
    else
    {
        fprintf(stderr, "error: Unrecognized 'release_versions' value.\n");
        return false;
    }


    pTarget->m_validFieldsBitmask |= RELEASE_VERSIONS_VALID;
    *ppCurr = pCurr;
    return true;
}

static bool parseExtraLabelsAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    return parseArrayAttribute(pTarget, pRaw, ppCurr, pEnd, 
                               &pTarget->m_extraLabels, "extra_labels", EXTRA_LABELS_VALID);
}

static bool parseExtraLabelsAddAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    return parseArrayAttribute(pTarget, pRaw, ppCurr, pEnd, 
                               &pTarget->m_extraLabelsAdd, "extra_labels_add", 0);
}

static bool parseMacrosAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    return parseArrayAttribute(pTarget, pRaw, ppCurr, pEnd,
                               &pTarget->m_macros, "macros", MACROS_VALID);
}

static bool parseMacrosAddAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    return parseArrayAttribute(pTarget, pRaw, ppCurr, pEnd, 
                               &pTarget->m_macrosAdd, "macro_add", 0);
}

static bool parseFeaturesAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    return parseArrayAttribute(pTarget, pRaw, ppCurr, pEnd,
                               &pTarget->m_features, "features", FEATURES_VALID);
}

static bool parseFeaturesAddAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    return parseArrayAttribute(pTarget, pRaw, ppCurr, pEnd,
                               &pTarget->m_featuresAdd, "features_add", 0);
}

static bool parseConfigObject(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    jsmntok_t*    pCurr = *ppCurr;
    
    if (pCurr >= pEnd)
    {
        assert ( pCurr < pEnd );
        return false;
    }

    // Should find start of config object.
    if (pCurr->type != JSMN_OBJECT)
    {
        fprintf(stderr, "error: Expected 'config' object.\n");
        return false;
    }
    int macroCount = pCurr->size;
    pCurr++;

    bool result = pTarget->m_configMacroNames.alloc(macroCount);
    if (!result)
    {
        fprintf(stderr, "error: Failed to allocate %d config macro names.\n", macroCount);
        return false;
    }
    result = pTarget->m_configMacroValues.alloc(macroCount);
    if (!result)
    {
        fprintf(stderr, "error: Failed to allocate %d config macro values.\n", macroCount);
        return false;
    }
    result = pTarget->m_configMacroHelp.alloc(macroCount);
    if (!result)
    {
        fprintf(stderr, "error: Failed to allocate %d config macro help strings.\n", macroCount);
        return false;
    }

    // Parse the config macro objects.
    for (int i = 0 ; i < macroCount ; i++)
    {
        result = parseConfigMacro(pTarget, i, pRaw, &pCurr, pEnd);
        if (!result)
        {
            fprintf(stderr, "error: Failed to parse config macro #%d.\n", i+1);
            return false;
        }
    }

    *ppCurr = pCurr;
    return true;
}

static bool parseConfigMacro(TargetObject* pTarget, int macroIndex, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    jsmntok_t* pCurr = *ppCurr;

    if (pCurr >= pEnd)
    {
        assert ( pCurr < pEnd );
        return false;
    }

    // Should be a named object.
    if (pCurr->type != JSMN_STRING)
    {
        fprintf(stderr, "error: Expected macro name.\n");
        return false;
    }
    SizedString objectName(pRaw + pCurr->start, pCurr->end - pCurr->start);
    pCurr++;

    // Should find start of config object.
    if (pCurr->type != JSMN_OBJECT)
    {
        fprintf(stderr, "error: Expected property object for \"%.*s\".\n",
                objectName.m_length, objectName.m_pVal);
        return false;
    }
    int elements = pCurr->size;
    pCurr++;

    for (int i = 0 ; i < elements ; i++)
    {
        bool result = parseConfigMacroElement(pTarget, macroIndex, pRaw, &pCurr, pEnd);
        if (!result)
        {
            fprintf(stderr, "error: Failed to parse element %d of \"%.*s\".\n",
                    i+1, objectName.m_length, objectName.m_pVal);
            return false;
        }
    }

    *ppCurr = pCurr;
    return true;
}

static bool parseConfigMacroElement(TargetObject* pTarget, int macroIndex, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    jsmntok_t* pCurr = *ppCurr;

    if (pCurr >= pEnd)
    {
        assert ( pCurr < pEnd );
        return false;
    }

    // Get macro element name.
    if (pCurr->type != JSMN_STRING)
    {
        fprintf(stderr, "error: Expected macro field name.\n");
        return false;
    }
    SizedString elementName(pRaw + pCurr->start, pCurr->end - pCurr->start);
    pCurr++;

    // Get macro element value.
    if (pCurr->type != JSMN_STRING && pCurr->type != JSMN_PRIMITIVE)
    {
        fprintf(stderr, "error: Expected string value for \"%.*s\" config macro field.\n",
                elementName.m_length, elementName.m_pVal);
        return false;
    }
    SizedString elementValue(pRaw + pCurr->start, pCurr->end - pCurr->start);
    pCurr++;

    if (0 == elementName.compare("help"))
    {
        pTarget->m_configMacroHelp[macroIndex].setName(elementValue);
    }
    else if (0 == elementName.compare("value"))
    {
        pTarget->m_configMacroValues[macroIndex].setName(elementValue);
    }
    else if (0 == elementName.compare("macro_name"))
    {
        pTarget->m_configMacroNames[macroIndex].setName(elementValue);
    }
    else
    {
        VERBOSE_LOG("verbose: Unexpected config field name \"%.*s\".\n", elementName.m_length, elementName.m_pVal);
    }

    *ppCurr = pCurr;
    return true;
}

static bool ignoreValue(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    jsmntok_t* pCurr = *ppCurr;

    if (pCurr >= pEnd)
    {
        assert ( pCurr < pEnd );
        return false;
    }

    int type = pCurr->type;
    bool result = true;
    switch (type)
    {
    case JSMN_OBJECT:
        result = ignoreObject(pTarget, pRaw, &pCurr, pEnd);
        break;
    case JSMN_ARRAY:
        result = ignoreArray(pTarget, pRaw, &pCurr, pEnd);
        break;
    case JSMN_STRING:
        pCurr++;
        break;
    case JSMN_PRIMITIVE:
        pCurr++;
        break;
    default:
        assert ( false );
        result = false;
        break;
    }

    *ppCurr = pCurr;
    return result;
}

static bool ignoreObject(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    jsmntok_t* pCurr = *ppCurr;

    if (pCurr >= pEnd)
    {
        assert ( pCurr < pEnd );
        return false;
    }

    assert ( pCurr->type == JSMN_OBJECT );
    int itemCount = pCurr->size;
    pCurr++;
    for (int i = 0 ; i < itemCount ; i++)
    {
        bool result = ignoreAttribute(pTarget, pRaw, &pCurr, pEnd);
        if (!result)
            return result;
    }

    *ppCurr = pCurr;
    return true;
}

static bool ignoreAttribute(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    jsmntok_t* pCurr = *ppCurr;

    if (pCurr >= pEnd)
    {
        assert ( pCurr < pEnd );
        return false;
    }

    // Should be a named attribute.
    if (pCurr->type != JSMN_STRING)
    {
        fprintf(stderr, "error: Expected attribute name while ignoring object.\n");
        return false;
    }
    pCurr++;

    bool result = ignoreValue(pTarget, pRaw, &pCurr, pEnd);
    if (!result)
        return false;

    *ppCurr = pCurr;
    return true;
}

static bool ignoreArray(TargetObject* pTarget, const char* pRaw, jsmntok_t** ppCurr, jsmntok_t* pEnd)
{
    jsmntok_t* pCurr = *ppCurr;

    if (pCurr >= pEnd)
    {
        assert ( pCurr < pEnd );
        return false;
    }

    assert ( pCurr->type == JSMN_ARRAY );
    int itemCount = pCurr->size;
    pCurr++;
    for (int i = 0 ; i < itemCount ; i++)
    {
        bool result = ignoreValue(pTarget, pRaw, &pCurr, pEnd);
        if (!result)
            return result;
    }

    *ppCurr = pCurr;
    return true;
}

static bool produceTargetMakefiles(const char* pOutputDirectory, Targets* pTargets)
{
    VERBOSE_LOG("verbose: Producing target makefiles....\n");

    int targetCount = pTargets->length();
    for (int i = 0 ; i < targetCount ; i++)
    {
        bool result = (*pTargets)[i].produceTargetMakefile(pOutputDirectory);
        if (!result)
            return false;
    }
    return true;
}

static bool produceHelloWorldMakefile(const char* pFilename, Targets* pTargets, uint32_t releaseVersion)
{
    VERBOSE_LOG("verbose: Producing %s....\n", pFilename);

    FILE* pFile = fopen(pFilename, "w");
    if (!pFile)
    {
        fprintf(stderr, "error: Failed to create %s\n", pFilename);
        perror(NULL);
        return false;
    }

    fprintf(pFile, "# This makefile was automatically generated by mbedUpdater.\n");

    fprintf(pFile, "PROJECT         := HelloWorld\n");
    fprintf(pFile, "DEVICES         := ");
    int targetCount = pTargets->length();
    for (int i = 0 ; i < targetCount ; i++)
    {
        TargetObject& target = (*pTargets)[i];
        if (target.m_isPublic && target.m_isGccSupported && (target.m_releaseVersionsBitmask & releaseVersion))
        {
            fprintf(pFile, "%.*s \\\n                   ", target.m_name.m_length, target.m_name.m_pVal);
        }
    }
    fprintf(pFile, "\n");

    fprintf(pFile, "GCC4MBED_DIR    := ../..\n\n");

    if (releaseVersion == MBED_2)
        fprintf(pFile, "MBED_OS_ENABLE  := 0\n");

    fprintf(pFile, "NO_FLOAT_SCANF  := 1\n");
    fprintf(pFile, "NO_FLOAT_PRINTF := 1\n\n");

    fprintf(pFile, "include $(GCC4MBED_DIR)/build/gcc4mbed.mk\n");

    fclose(pFile);
    pFile = NULL;
    return true;
}
