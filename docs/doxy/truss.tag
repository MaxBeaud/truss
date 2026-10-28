<?xml version='1.0' encoding='UTF-8' standalone='yes' ?>
<tagfile doxygen_version="1.18.0">
  <compound kind="file">
    <name>internals.md</name>
    <path>docs/</path>
    <filename>internals_8md.html</filename>
  </compound>
  <compound kind="file">
    <name>api.cppm</name>
    <path>src/</path>
    <filename>api_8cppm.html</filename>
    <class kind="struct">truss::FetchOptions</class>
    <class kind="struct">truss::Package</class>
    <class kind="struct">truss::Error</class>
    <class kind="class">truss::generator</class>
    <class kind="struct">truss::generator::promise_type</class>
    <class kind="class">truss::generator::iterator</class>
    <class kind="struct">truss::TargetTriple</class>
    <class kind="struct">truss::os::ProcessResult</class>
    <class kind="struct">truss::os::RunOptions</class>
    <class kind="class">truss::os::temp_scope</class>
    <class kind="struct">truss::fs::WalkOptions</class>
    <class kind="class">truss::Config</class>
    <class kind="class">truss::Target</class>
    <class kind="class">truss::Executable</class>
    <class kind="class">truss::StaticLib</class>
    <class kind="class">truss::SharedLib</class>
    <class kind="class">truss::Interface</class>
    <class kind="class">truss::TargetRef</class>
    <class kind="struct">truss::TaskContext</class>
    <class kind="class">truss::Project</class>
    <namespace>truss</namespace>
    <namespace>truss::os</namespace>
    <namespace>truss::fs</namespace>
    <namespace>truss::log</namespace>
    <concept>truss::StringRange</concept>
    <concept>truss::os::EnvParsable</concept>
  </compound>
  <compound kind="file">
    <name>impl.cppm</name>
    <path>src/</path>
    <filename>impl_8cppm.html</filename>
    <class kind="struct">truss::detail::ProcOutcome</class>
    <class kind="struct">truss::detail::ModuleUnitInfo</class>
    <class kind="struct">truss::detail::FlagSet</class>
    <class kind="struct">truss::detail::LinkFlags</class>
    <class kind="class">truss::detail::NinjaWriter</class>
    <class kind="struct">truss::detail::NinjaWriter::Edge</class>
    <namespace>truss</namespace>
    <namespace>truss::detail</namespace>
  </compound>
  <compound kind="class">
    <name>truss::Config</name>
    <filename>classtruss_1_1Config.html</filename>
    <member kind="function">
      <type>Config &amp;</type>
      <name>compiler</name>
      <anchorfile>classtruss_1_1Config.html</anchorfile>
      <anchor>a229095a0d8c80383c71acb7273f8dfb0</anchor>
      <arglist>(std::string name_or_path)</arglist>
    </member>
    <member kind="function">
      <type>Config &amp;</type>
      <name>linker</name>
      <anchorfile>classtruss_1_1Config.html</anchorfile>
      <anchor>a3f08d4157cf513aa2e1f9f0cf14791ae</anchor>
      <arglist>(std::string name_or_path)</arglist>
    </member>
    <member kind="function">
      <type>Config &amp;</type>
      <name>ar</name>
      <anchorfile>classtruss_1_1Config.html</anchorfile>
      <anchor>aae11d9544c557d2675cc02d64704a069</anchor>
      <arglist>(std::string name_or_path)</arglist>
    </member>
    <member kind="function">
      <type>Config &amp;</type>
      <name>stdlib</name>
      <anchorfile>classtruss_1_1Config.html</anchorfile>
      <anchor>a70f7a7a50b704edbb8f14953ea8adaac</anchor>
      <arglist>(StdLib s)</arglist>
    </member>
    <member kind="function">
      <type>Config &amp;</type>
      <name>launcher</name>
      <anchorfile>classtruss_1_1Config.html</anchorfile>
      <anchor>af88346ea25823e7ba3fc961b6d3d63a3</anchor>
      <arglist>(std::string name)</arglist>
    </member>
    <member kind="function">
      <type>Config &amp;</type>
      <name>toolchain</name>
      <anchorfile>classtruss_1_1Config.html</anchorfile>
      <anchor>a07320115a2de0c8be818cf47e18bcdd6</anchor>
      <arglist>(Toolchain t)</arglist>
    </member>
    <member kind="function">
      <type>const std::string &amp;</type>
      <name>compiler</name>
      <anchorfile>classtruss_1_1Config.html</anchorfile>
      <anchor>abc8fe275e95c28fe6f65a1bc3dc42843</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::string &amp;</type>
      <name>linker</name>
      <anchorfile>classtruss_1_1Config.html</anchorfile>
      <anchor>a3c4419ec9cfd187ebd972a3878ff1083</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::string &amp;</type>
      <name>ar</name>
      <anchorfile>classtruss_1_1Config.html</anchorfile>
      <anchor>a302324d7c0bc17635c4a0203122620fa</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::string &amp;</type>
      <name>launcher</name>
      <anchorfile>classtruss_1_1Config.html</anchorfile>
      <anchor>ad7503e569da703402c66ecf92f551fb0</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>bool</type>
      <name>launcher_set</name>
      <anchorfile>classtruss_1_1Config.html</anchorfile>
      <anchor>a5f446325314b9e79a904fcc49d8ba9fd</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>StdLib</type>
      <name>stdlib</name>
      <anchorfile>classtruss_1_1Config.html</anchorfile>
      <anchor>a3457d07242d3860a02e314fdbcad9c6c</anchor>
      <arglist>() const</arglist>
    </member>
  </compound>
  <compound kind="struct">
    <name>truss::detail::NinjaWriter::Edge</name>
    <filename>structtruss_1_1detail_1_1NinjaWriter_1_1Edge.html</filename>
    <member kind="variable">
      <type>std::vector&lt; std::string &gt;</type>
      <name>outputs</name>
      <anchorfile>structtruss_1_1detail_1_1NinjaWriter_1_1Edge.html</anchorfile>
      <anchor>aca7ee4ae8e1e83adcdddef747f3ce008</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::vector&lt; std::string &gt;</type>
      <name>implicit_outs</name>
      <anchorfile>structtruss_1_1detail_1_1NinjaWriter_1_1Edge.html</anchorfile>
      <anchor>aab3f730dd601fa2d8d9df9109b629cb9</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::string</type>
      <name>rule</name>
      <anchorfile>structtruss_1_1detail_1_1NinjaWriter_1_1Edge.html</anchorfile>
      <anchor>ab0a1ab0f5411ec8e6738ccdf88fadbb6</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::vector&lt; std::string &gt;</type>
      <name>inputs</name>
      <anchorfile>structtruss_1_1detail_1_1NinjaWriter_1_1Edge.html</anchorfile>
      <anchor>af436d9e2c3af565d8889691e7793ecbe</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::vector&lt; std::string &gt;</type>
      <name>implicit_ins</name>
      <anchorfile>structtruss_1_1detail_1_1NinjaWriter_1_1Edge.html</anchorfile>
      <anchor>a420a9e17583a63e2103e7ed55d05bd9a</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::vector&lt; std::pair&lt; std::string, std::string &gt; &gt;</type>
      <name>vars</name>
      <anchorfile>structtruss_1_1detail_1_1NinjaWriter_1_1Edge.html</anchorfile>
      <anchor>a9aede420e19d686030a567929a0a52f8</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="struct">
    <name>truss::Error</name>
    <filename>structtruss_1_1Error.html</filename>
    <member kind="enumeration">
      <type></type>
      <name>Kind</name>
      <anchorfile>structtruss_1_1Error.html</anchorfile>
      <anchor>af83652ed455cdf9b4e3636e269f2976f</anchor>
      <arglist></arglist>
      <enumvalue file="structtruss_1_1Error.html" anchor="af83652ed455cdf9b4e3636e269f2976fa42a02b42ec22cfcbd78aaf7a66a5ee09">Io</enumvalue>
      <enumvalue file="structtruss_1_1Error.html" anchor="af83652ed455cdf9b4e3636e269f2976fafa535ffb25e1fd20341652f9be21e06e">Config</enumvalue>
      <enumvalue file="structtruss_1_1Error.html" anchor="af83652ed455cdf9b4e3636e269f2976faee97be03cb04119af45014d815621ce1">Command</enumvalue>
      <enumvalue file="structtruss_1_1Error.html" anchor="af83652ed455cdf9b4e3636e269f2976fa4ea7755f84f02d23876d90be2f9765cf">Fetch</enumvalue>
      <enumvalue file="structtruss_1_1Error.html" anchor="af83652ed455cdf9b4e3636e269f2976fad6e6eba8da557ba999767938e659b923">Toolchain</enumvalue>
      <enumvalue file="structtruss_1_1Error.html" anchor="af83652ed455cdf9b4e3636e269f2976faafbf0897a5a83fdd873dfb032ec695d3">Internal</enumvalue>
    </member>
    <member kind="variable">
      <type>Kind</type>
      <name>kind</name>
      <anchorfile>structtruss_1_1Error.html</anchorfile>
      <anchor>a7f16baab861ad6c667315d7e03191169</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::string</type>
      <name>message</name>
      <anchorfile>structtruss_1_1Error.html</anchorfile>
      <anchor>abb5b65d2b7136c7a07d6fd9dd265cfce</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::optional&lt; std::filesystem::path &gt;</type>
      <name>path</name>
      <anchorfile>structtruss_1_1Error.html</anchorfile>
      <anchor>a23aae9bedf6f68e2a21005aaa973abf5</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="class">
    <name>truss::Executable</name>
    <filename>classtruss_1_1Executable.html</filename>
    <base>truss::Target</base>
    <member kind="function">
      <type></type>
      <name>Executable</name>
      <anchorfile>classtruss_1_1Executable.html</anchorfile>
      <anchor>a62186d1a45317311ab57b7060a7c2918</anchor>
      <arglist>(std::string name)</arglist>
    </member>
  </compound>
  <compound kind="struct">
    <name>truss::FetchOptions</name>
    <filename>structtruss_1_1FetchOptions.html</filename>
    <member kind="variable">
      <type>std::string</type>
      <name>git</name>
      <anchorfile>structtruss_1_1FetchOptions.html</anchorfile>
      <anchor>a4ec525de91f6403ed100a71ae9468140</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::string</type>
      <name>tag</name>
      <anchorfile>structtruss_1_1FetchOptions.html</anchorfile>
      <anchor>a1ede6da1a86364b8469b289a8bb71196</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::string</type>
      <name>url</name>
      <anchorfile>structtruss_1_1FetchOptions.html</anchorfile>
      <anchor>a9c3142e028838b2d0ccf2ad630c098e3</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::string</type>
      <name>sha256</name>
      <anchorfile>structtruss_1_1FetchOptions.html</anchorfile>
      <anchor>a80822a0f1ff7145c178b8ad8d6222dde</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="struct">
    <name>truss::detail::FlagSet</name>
    <filename>structtruss_1_1detail_1_1FlagSet.html</filename>
    <member kind="variable">
      <type>std::string</type>
      <name>compile</name>
      <anchorfile>structtruss_1_1detail_1_1FlagSet.html</anchorfile>
      <anchor>a033f065990ae0ad7f90ba963fcc015ae</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::string</type>
      <name>compile_c</name>
      <anchorfile>structtruss_1_1detail_1_1FlagSet.html</anchorfile>
      <anchor>ac3398b621b571d214ebdb06c417fff89</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="class">
    <name>truss::generator</name>
    <filename>classtruss_1_1generator.html</filename>
    <templarg>class T</templarg>
    <class kind="struct">truss::generator::promise_type</class>
    <class kind="class">truss::generator::iterator</class>
    <member kind="typedef">
      <type>std::coroutine_handle&lt; promise_type &gt;</type>
      <name>handle_t</name>
      <anchorfile>classtruss_1_1generator.html</anchorfile>
      <anchor>aaf8b7e2b7ce0154669427717e476e682</anchor>
      <arglist></arglist>
    </member>
    <member kind="function">
      <type></type>
      <name>generator</name>
      <anchorfile>classtruss_1_1generator.html</anchorfile>
      <anchor>a01f8306db64bb5b14da2b9e3059abfea</anchor>
      <arglist>(handle_t h)</arglist>
    </member>
    <member kind="function">
      <type></type>
      <name>generator</name>
      <anchorfile>classtruss_1_1generator.html</anchorfile>
      <anchor>a50514921d794d01765760fe54e547845</anchor>
      <arglist>(const generator &amp;)=delete</arglist>
    </member>
    <member kind="function">
      <type>generator &amp;</type>
      <name>operator=</name>
      <anchorfile>classtruss_1_1generator.html</anchorfile>
      <anchor>ac23507406152441cdc1a6327e0f0075d</anchor>
      <arglist>(const generator &amp;)=delete</arglist>
    </member>
    <member kind="function">
      <type></type>
      <name>generator</name>
      <anchorfile>classtruss_1_1generator.html</anchorfile>
      <anchor>a01894db0a1faab66d3feb088bce2a27e</anchor>
      <arglist>(generator &amp;&amp;other) noexcept</arglist>
    </member>
    <member kind="function">
      <type>generator &amp;</type>
      <name>operator=</name>
      <anchorfile>classtruss_1_1generator.html</anchorfile>
      <anchor>abb7cf93e817b429951b0665547d45c76</anchor>
      <arglist>(generator &amp;&amp;other) noexcept</arglist>
    </member>
    <member kind="function">
      <type></type>
      <name>~generator</name>
      <anchorfile>classtruss_1_1generator.html</anchorfile>
      <anchor>a1bf1100e0259afcbc2fcf8c884cf69f6</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>iterator</type>
      <name>begin</name>
      <anchorfile>classtruss_1_1generator.html</anchorfile>
      <anchor>a3dee5975190effbca186f195d7e1ebbf</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>std::default_sentinel_t</type>
      <name>end</name>
      <anchorfile>classtruss_1_1generator.html</anchorfile>
      <anchor>a6c5fe2de5652ee476b6deab19b944f9f</anchor>
      <arglist>()</arglist>
    </member>
  </compound>
  <compound kind="class">
    <name>truss::Interface</name>
    <filename>classtruss_1_1Interface.html</filename>
    <base>truss::Target</base>
    <member kind="function">
      <type></type>
      <name>Interface</name>
      <anchorfile>classtruss_1_1Interface.html</anchorfile>
      <anchor>ab9ce7fb6b59734caa9b83da6d368a84b</anchor>
      <arglist>(std::string name)</arglist>
    </member>
  </compound>
  <compound kind="class">
    <name>truss::generator::iterator</name>
    <filename>classtruss_1_1generator_1_1iterator.html</filename>
    <member kind="typedef">
      <type>std::input_iterator_tag</type>
      <name>iterator_category</name>
      <anchorfile>classtruss_1_1generator_1_1iterator.html</anchorfile>
      <anchor>a4ff1f014d0f498ed69174098746cddb3</anchor>
      <arglist></arglist>
    </member>
    <member kind="typedef">
      <type>T</type>
      <name>value_type</name>
      <anchorfile>classtruss_1_1generator_1_1iterator.html</anchorfile>
      <anchor>ae04492f8fb705513951edfff91643394</anchor>
      <arglist></arglist>
    </member>
    <member kind="typedef">
      <type>std::ptrdiff_t</type>
      <name>difference_type</name>
      <anchorfile>classtruss_1_1generator_1_1iterator.html</anchorfile>
      <anchor>a79f34b7e954cf1298cb7c5d8a1f0ed72</anchor>
      <arglist></arglist>
    </member>
    <member kind="typedef">
      <type>const T &amp;</type>
      <name>reference</name>
      <anchorfile>classtruss_1_1generator_1_1iterator.html</anchorfile>
      <anchor>a503eafa32bb5b998174d79d3dccce6ce</anchor>
      <arglist></arglist>
    </member>
    <member kind="typedef">
      <type>const T *</type>
      <name>pointer</name>
      <anchorfile>classtruss_1_1generator_1_1iterator.html</anchorfile>
      <anchor>aa2ee56fad2631d24d39b4c718fbd5907</anchor>
      <arglist></arglist>
    </member>
    <member kind="function">
      <type></type>
      <name>iterator</name>
      <anchorfile>classtruss_1_1generator_1_1iterator.html</anchorfile>
      <anchor>a58672f4e6714bff4ab9eac7e41b1ae80</anchor>
      <arglist>()=default</arglist>
    </member>
    <member kind="function">
      <type></type>
      <name>iterator</name>
      <anchorfile>classtruss_1_1generator_1_1iterator.html</anchorfile>
      <anchor>ac3267f537a054e73b5553c589959fd9f</anchor>
      <arglist>(handle_t h)</arglist>
    </member>
    <member kind="function">
      <type>const T &amp;</type>
      <name>operator*</name>
      <anchorfile>classtruss_1_1generator_1_1iterator.html</anchorfile>
      <anchor>a3da48dc1bac076360ba82bef7f7b7335</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const T *</type>
      <name>operator-&gt;</name>
      <anchorfile>classtruss_1_1generator_1_1iterator.html</anchorfile>
      <anchor>a574d2a68493a9891e63c1b608db2ec81</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>iterator &amp;</type>
      <name>operator++</name>
      <anchorfile>classtruss_1_1generator_1_1iterator.html</anchorfile>
      <anchor>a016f0b413ec3c6e799f9476e950a48cd</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>iterator</type>
      <name>operator++</name>
      <anchorfile>classtruss_1_1generator_1_1iterator.html</anchorfile>
      <anchor>a46d178076b76edbf7d549be2c6242233</anchor>
      <arglist>(int)</arglist>
    </member>
    <member kind="friend">
      <type>friend bool</type>
      <name>operator==</name>
      <anchorfile>classtruss_1_1generator_1_1iterator.html</anchorfile>
      <anchor>a862ad61b8e374e951eb51b200eaebf5b</anchor>
      <arglist>(const iterator &amp;it, std::default_sentinel_t)</arglist>
    </member>
    <member kind="friend">
      <type>friend bool</type>
      <name>operator==</name>
      <anchorfile>classtruss_1_1generator_1_1iterator.html</anchorfile>
      <anchor>a87aa8eb4bdaac98dedc068119328ed3a</anchor>
      <arglist>(std::default_sentinel_t s, const iterator &amp;it)</arglist>
    </member>
    <member kind="friend">
      <type>friend bool</type>
      <name>operator!=</name>
      <anchorfile>classtruss_1_1generator_1_1iterator.html</anchorfile>
      <anchor>acb9292a7418d223c30d24429a4c4d948</anchor>
      <arglist>(const iterator &amp;it, std::default_sentinel_t s)</arglist>
    </member>
    <member kind="friend">
      <type>friend bool</type>
      <name>operator!=</name>
      <anchorfile>classtruss_1_1generator_1_1iterator.html</anchorfile>
      <anchor>a70d3bcbdcda6c774cf690d634a52eb2a</anchor>
      <arglist>(std::default_sentinel_t s, const iterator &amp;it)</arglist>
    </member>
  </compound>
  <compound kind="struct">
    <name>truss::detail::LinkFlags</name>
    <filename>structtruss_1_1detail_1_1LinkFlags.html</filename>
    <member kind="variable">
      <type>std::string</type>
      <name>driver</name>
      <anchorfile>structtruss_1_1detail_1_1LinkFlags.html</anchorfile>
      <anchor>a40125dc5a52a852e65874245d903ea8e</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::string</type>
      <name>linker</name>
      <anchorfile>structtruss_1_1detail_1_1LinkFlags.html</anchorfile>
      <anchor>ac96571685ebbaf9866e433e615399da1</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="struct">
    <name>truss::detail::ModuleUnitInfo</name>
    <filename>structtruss_1_1detail_1_1ModuleUnitInfo.html</filename>
    <member kind="variable">
      <type>std::string</type>
      <name>path</name>
      <anchorfile>structtruss_1_1detail_1_1ModuleUnitInfo.html</anchorfile>
      <anchor>aa61c980d341c9f2cc4cce9f203cbf43c</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::string</type>
      <name>module_name</name>
      <anchorfile>structtruss_1_1detail_1_1ModuleUnitInfo.html</anchorfile>
      <anchor>a3c258c49751d154d3a87e745bc91ad29</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>bool</type>
      <name>is_bmi_source</name>
      <anchorfile>structtruss_1_1detail_1_1ModuleUnitInfo.html</anchorfile>
      <anchor>a74b0b78ce8f70444eb6d410095e08e49</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::vector&lt; std::string &gt;</type>
      <name>imports</name>
      <anchorfile>structtruss_1_1detail_1_1ModuleUnitInfo.html</anchorfile>
      <anchor>a44c7b0326adfdb9338e1af3bf4982294</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="class">
    <name>truss::detail::NinjaWriter</name>
    <filename>classtruss_1_1detail_1_1NinjaWriter.html</filename>
    <class kind="struct">truss::detail::NinjaWriter::Edge</class>
    <member kind="function">
      <type>void</type>
      <name>comment</name>
      <anchorfile>classtruss_1_1detail_1_1NinjaWriter.html</anchorfile>
      <anchor>a8d65b626c113305bd0d6fc69574c8137</anchor>
      <arglist>(std::string_view text)</arglist>
    </member>
    <member kind="function">
      <type>void</type>
      <name>var</name>
      <anchorfile>classtruss_1_1detail_1_1NinjaWriter.html</anchorfile>
      <anchor>a38fab417a768cd524ff3a9bc61cf8655</anchor>
      <arglist>(std::string_view name, std::string_view value)</arglist>
    </member>
    <member kind="function">
      <type>void</type>
      <name>rule</name>
      <anchorfile>classtruss_1_1detail_1_1NinjaWriter.html</anchorfile>
      <anchor>a78a6002f6d9698d2b895ea4b6e55e799</anchor>
      <arglist>(std::string_view name, std::vector&lt; std::pair&lt; std::string, std::string &gt; &gt; body)</arglist>
    </member>
    <member kind="function">
      <type>void</type>
      <name>build</name>
      <anchorfile>classtruss_1_1detail_1_1NinjaWriter.html</anchorfile>
      <anchor>a030049ebad0bd238d3d53e5619e59bda</anchor>
      <arglist>(Edge e)</arglist>
    </member>
    <member kind="function">
      <type>void</type>
      <name>phony</name>
      <anchorfile>classtruss_1_1detail_1_1NinjaWriter.html</anchorfile>
      <anchor>ad472a6f8d5cbf8a3206cb164cfe597a5</anchor>
      <arglist>(std::string_view name, std::vector&lt; std::string &gt; ins)</arglist>
    </member>
    <member kind="function">
      <type>void</type>
      <name>default_targets</name>
      <anchorfile>classtruss_1_1detail_1_1NinjaWriter.html</anchorfile>
      <anchor>a616fc8d9bf4126a55bbe61ba2a8e2c8c</anchor>
      <arglist>(std::vector&lt; std::string &gt; names)</arglist>
    </member>
    <member kind="function">
      <type>const std::string &amp;</type>
      <name>str</name>
      <anchorfile>classtruss_1_1detail_1_1NinjaWriter.html</anchorfile>
      <anchor>a4fb572fe9293c8127181217718508c2a</anchor>
      <arglist>() const</arglist>
    </member>
  </compound>
  <compound kind="struct">
    <name>truss::Package</name>
    <filename>structtruss_1_1Package.html</filename>
    <member kind="variable">
      <type>std::string</type>
      <name>name</name>
      <anchorfile>structtruss_1_1Package.html</anchorfile>
      <anchor>a607516b7ff7283ba265847124556c55a</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::filesystem::path</type>
      <name>dir</name>
      <anchorfile>structtruss_1_1Package.html</anchorfile>
      <anchor>a77e9307403de2cefd4a0ff35890f71ad</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="struct">
    <name>truss::os::ProcessResult</name>
    <filename>structtruss_1_1os_1_1ProcessResult.html</filename>
    <member kind="variable">
      <type>int</type>
      <name>exit_code</name>
      <anchorfile>structtruss_1_1os_1_1ProcessResult.html</anchorfile>
      <anchor>adbd01136a247d4af55c9f00a385f2597</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::string</type>
      <name>output</name>
      <anchorfile>structtruss_1_1os_1_1ProcessResult.html</anchorfile>
      <anchor>afb168bc9831e25cd32c927e89bd49334</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="struct">
    <name>truss::detail::ProcOutcome</name>
    <filename>structtruss_1_1detail_1_1ProcOutcome.html</filename>
    <member kind="variable">
      <type>int</type>
      <name>exit_code</name>
      <anchorfile>structtruss_1_1detail_1_1ProcOutcome.html</anchorfile>
      <anchor>af16038552623f1334db891703da63973</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::string</type>
      <name>output</name>
      <anchorfile>structtruss_1_1detail_1_1ProcOutcome.html</anchorfile>
      <anchor>a16b549928a541099562d3c6bf1d4a804</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="class">
    <name>truss::Project</name>
    <filename>classtruss_1_1Project.html</filename>
    <member kind="function">
      <type></type>
      <name>Project</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a49c94664ff8cc2a5201f05b76edd196d</anchor>
      <arglist>(std::string name, std::span&lt; char *const &gt; argv)</arglist>
    </member>
    <member kind="function">
      <type></type>
      <name>Project</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>ac4f149ae42f5fd5a1526d980aef030a3</anchor>
      <arglist>(std::string name, int argc, char **argv)</arglist>
    </member>
    <member kind="function">
      <type>Executable &amp;</type>
      <name>add_executable</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a91d48b992c26d9210140580e1f2c814c</anchor>
      <arglist>(std::string name)</arglist>
    </member>
    <member kind="function">
      <type>StaticLib &amp;</type>
      <name>add_static_library</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>af4900a3ffd423d730ea64715ea4fd7f7</anchor>
      <arglist>(std::string name)</arglist>
    </member>
    <member kind="function">
      <type>SharedLib &amp;</type>
      <name>add_shared_library</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>af79b9fa7f77cae811c2786e43c3a841f</anchor>
      <arglist>(std::string name)</arglist>
    </member>
    <member kind="function">
      <type>Interface &amp;</type>
      <name>add_interface_library</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a93848a559208925a3a515506549df588</anchor>
      <arglist>(std::string name)</arglist>
    </member>
    <member kind="function">
      <type>void</type>
      <name>task</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a478e60eb1f741c27fa4cef30180d4988</anchor>
      <arglist>(std::string name, TaskFn fn)</arglist>
    </member>
    <member kind="function">
      <type>Project &amp;</type>
      <name>pre_build</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a541a92f743431738b319b196b5df2abc</anchor>
      <arglist>(TaskFn fn)</arglist>
    </member>
    <member kind="function">
      <type>Project &amp;</type>
      <name>post_build</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a2392568ba8bc7ebad1a1fd11c2907499</anchor>
      <arglist>(TaskFn fn)</arglist>
    </member>
    <member kind="function">
      <type>Project &amp;</type>
      <name>configure</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a55a34d383130bfb42263710d8cdfd1ad</anchor>
      <arglist>(const Config &amp;conf)</arglist>
    </member>
    <member kind="function">
      <type>const Config &amp;</type>
      <name>config</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>abfa4bc2ffcc2f112fd22315f8ee1e7f6</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; Package &gt;</type>
      <name>fetch</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a014fb0c54e2e7f9a03aa338480dc7117</anchor>
      <arglist>(std::string_view name, const FetchOptions &amp;opts)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; std::vector&lt; Package &gt; &gt;</type>
      <name>fetch_all</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a975fd8ef9b3c40d0db924c66cc1f2fec</anchor>
      <arglist>(std::vector&lt; std::pair&lt; std::string, FetchOptions &gt; &gt; requests)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; TargetRef &gt;</type>
      <name>add_subdirectory</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a886029732fe415cf0d1c9163c8b32c95</anchor>
      <arglist>(const std::filesystem::path &amp;dir)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; TargetRef &gt;</type>
      <name>add_subdirectory</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a87ad77b60825a978b9c74d08f8faea77</anchor>
      <arglist>(const Package &amp;pkg)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; TargetRef &gt;</type>
      <name>conan</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>ade9cbe2815224f69a5cca0a614b95add</anchor>
      <arglist>(std::string_view conanfile=&quot;.&quot;)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; TargetRef &gt;</type>
      <name>conan</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>aaa67a853297998ad4a16ced936e7e87b</anchor>
      <arglist>(std::initializer_list&lt; std::string_view &gt; refs)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; TargetRef &gt;</type>
      <name>vcpkg</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a071855db1e3953df8354b24156580e0f</anchor>
      <arglist>(std::vector&lt; std::string &gt; packages)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>build</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>aa6a306781fefc73d66c1d4bc540790f9</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>compile_commands</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>af3d536060e57a39273a4d4bc165cafb2</anchor>
      <arglist>(const std::filesystem::path &amp;path=&quot;.&quot;)</arglist>
    </member>
    <member kind="function">
      <type>const std::string &amp;</type>
      <name>name</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>aca754070b3b144e5f0d5db05767addd0</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>BuildMode</type>
      <name>mode</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>ac78cb537cdaa30a0f72ce0c8e4e23926</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::filesystem::path &amp;</type>
      <name>cwd</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a5929335db45f0c6dd79b45dc5c9bca07</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>std::filesystem::path</type>
      <name>build_dir</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>af5e926ab2e4d98456c4d7e396357f14c</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>int</type>
      <name>jobs</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a37a1dd4db1d18c1724bec92e89bfdb25</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>bool</type>
      <name>verbose</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a38bf4f182419a9ffd5478256769ba6a7</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::vector&lt; std::string &gt; &amp;</type>
      <name>only_targets</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a56e9874e8d2cfdfcbd2ef77eeeeceb78</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::vector&lt; std::filesystem::path &gt; &amp;</type>
      <name>truss_module_paths</name>
      <anchorfile>classtruss_1_1Project.html</anchorfile>
      <anchor>a1820bbf9b8100623f9da3236de9e7571</anchor>
      <arglist>() const</arglist>
    </member>
  </compound>
  <compound kind="struct">
    <name>truss::generator::promise_type</name>
    <filename>structtruss_1_1generator_1_1promise__type.html</filename>
    <member kind="function">
      <type>generator</type>
      <name>get_return_object</name>
      <anchorfile>structtruss_1_1generator_1_1promise__type.html</anchorfile>
      <anchor>adac28ef7c630cfb7d5414f0848a3bcc4</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>std::suspend_always</type>
      <name>initial_suspend</name>
      <anchorfile>structtruss_1_1generator_1_1promise__type.html</anchorfile>
      <anchor>a6db64f251e20d899f80eaf3d8586f271</anchor>
      <arglist>() noexcept</arglist>
    </member>
    <member kind="function">
      <type>std::suspend_always</type>
      <name>final_suspend</name>
      <anchorfile>structtruss_1_1generator_1_1promise__type.html</anchorfile>
      <anchor>aa56ea40ee649f56dc643e0fdf225fe27</anchor>
      <arglist>() noexcept</arglist>
    </member>
    <member kind="function">
      <type>std::suspend_always</type>
      <name>yield_value</name>
      <anchorfile>structtruss_1_1generator_1_1promise__type.html</anchorfile>
      <anchor>a89a3c7f4276fef9515be2a6f2834c6d1</anchor>
      <arglist>(T v)</arglist>
    </member>
    <member kind="function">
      <type>void</type>
      <name>return_void</name>
      <anchorfile>structtruss_1_1generator_1_1promise__type.html</anchorfile>
      <anchor>a67a9d23cd233de5f4f4a040eab065090</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>void</type>
      <name>unhandled_exception</name>
      <anchorfile>structtruss_1_1generator_1_1promise__type.html</anchorfile>
      <anchor>ab81f2d29d7fc044309c9172274b14a10</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="variable">
      <type>std::optional&lt; T &gt;</type>
      <name>value</name>
      <anchorfile>structtruss_1_1generator_1_1promise__type.html</anchorfile>
      <anchor>ab850e43c1d60fdfa5a4df1fe6c1a4e5c</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::exception_ptr</type>
      <name>exception</name>
      <anchorfile>structtruss_1_1generator_1_1promise__type.html</anchorfile>
      <anchor>a94c0831b85680fb4886247f799e1581a</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="struct">
    <name>truss::os::RunOptions</name>
    <filename>structtruss_1_1os_1_1RunOptions.html</filename>
    <member kind="variable">
      <type>std::filesystem::path</type>
      <name>cwd</name>
      <anchorfile>structtruss_1_1os_1_1RunOptions.html</anchorfile>
      <anchor>a848e326647afa61a4aa1f2764ab9f144</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="class">
    <name>truss::SharedLib</name>
    <filename>classtruss_1_1SharedLib.html</filename>
    <base>truss::Target</base>
    <member kind="function">
      <type></type>
      <name>SharedLib</name>
      <anchorfile>classtruss_1_1SharedLib.html</anchorfile>
      <anchor>ac92b5971331a8d5437569cfa89cdd6e1</anchor>
      <arglist>(std::string name)</arglist>
    </member>
  </compound>
  <compound kind="class">
    <name>truss::StaticLib</name>
    <filename>classtruss_1_1StaticLib.html</filename>
    <base>truss::Target</base>
    <member kind="function">
      <type></type>
      <name>StaticLib</name>
      <anchorfile>classtruss_1_1StaticLib.html</anchorfile>
      <anchor>a45da6fe47370d45a3a8f30da8a3e5535</anchor>
      <arglist>(std::string name)</arglist>
    </member>
  </compound>
  <compound kind="class">
    <name>truss::Target</name>
    <filename>classtruss_1_1Target.html</filename>
    <member kind="function">
      <type></type>
      <name>Target</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a35b066bd44861332c87efc11b8d98081</anchor>
      <arglist>(std::string name, TargetKind kind)</arglist>
    </member>
    <member kind="function" virtualness="virtual">
      <type>virtual</type>
      <name>~Target</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a5adfc1b0425cd5a46491502f0203f6f6</anchor>
      <arglist>()=default</arglist>
    </member>
    <member kind="function">
      <type></type>
      <name>Target</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a0f8773913aba96c67baaaa0d67d1b2f0</anchor>
      <arglist>(const Target &amp;)=delete</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>operator=</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a847d6d96e2e086ca26832d8b19046a93</anchor>
      <arglist>(const Target &amp;)=delete</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>sources</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a345d89ef4d878cd635d9b946dcc42ae2</anchor>
      <arglist>(std::string_view pattern)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>sources</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>affa264839f85490a6da566e9ade4f87d</anchor>
      <arglist>(std::initializer_list&lt; std::string_view &gt; patterns)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>sources</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a307657174108d5eff04945645ad5f952</anchor>
      <arglist>(R &amp;&amp;patterns)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>sources_in</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a0ba699127790b2da753c421f349495c9</anchor>
      <arglist>(std::string_view dir)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>remove_source</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>af1e2bca2f16d466a3f94b8074c4b75eb</anchor>
      <arglist>(std::string_view path)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>include_dirs</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>ab21ba3606287142f18ba0e51d45c3396</anchor>
      <arglist>(std::string_view dir)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>include_dirs</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>add82fa774efc0a52d731922bef43de93</anchor>
      <arglist>(std::initializer_list&lt; std::string_view &gt; dirs)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>include_dirs</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a168a7a35da8521049dc508310f40ff05</anchor>
      <arglist>(R &amp;&amp;dirs)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>defines</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a6fa3ba064dd5cc72148920f403dc9d04</anchor>
      <arglist>(std::string_view def)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>defines</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a4a8eab24b99faf50ae20d58579be33f8</anchor>
      <arglist>(std::initializer_list&lt; std::string_view &gt; defs)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>defines</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>af071ab11e2def2355ecc69abfd6cf4eb</anchor>
      <arglist>(R &amp;&amp;defs)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>link_libs</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a830991aa7ab88f1f1840bd69c58a346d</anchor>
      <arglist>(std::string_view lib)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>link_libs</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a30ad4c98ef2fcb56d99401503247d468</anchor>
      <arglist>(std::initializer_list&lt; std::string_view &gt; libs)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>link_libs</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>ae84ec5a4f027a243b814d43df8b5b66f</anchor>
      <arglist>(R &amp;&amp;libs)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>link_paths</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a0b0769c9746a6b8cf1d76b39b214564d</anchor>
      <arglist>(std::string_view path)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>link_paths</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a3651dbb358dd4098f3ad76d11be62572</anchor>
      <arglist>(std::initializer_list&lt; std::string_view &gt; paths)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>link_paths</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>aa2c04b2a1496351802a0dbcfc013032b</anchor>
      <arglist>(R &amp;&amp;paths)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>link_frameworks</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a2f0f2949e868ba4394cbf3317278cb88</anchor>
      <arglist>(std::string_view fw)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>link_frameworks</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a1ccb32c6b816f20f71f69c318271bfb5</anchor>
      <arglist>(std::initializer_list&lt; std::string_view &gt; fw)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>link_frameworks</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a4668800909c2383738a43f092b3b0020</anchor>
      <arglist>(R &amp;&amp;fw)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>framework_paths</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a0179a7516c9dd6ccd46835335b097d5a</anchor>
      <arglist>(std::string_view path)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>framework_paths</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>aadc92a2a4d922becf1cddcd5412f4e2d</anchor>
      <arglist>(std::initializer_list&lt; std::string_view &gt; paths)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>framework_paths</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>afa544898d7d6885e3f24d2999c156a3a</anchor>
      <arglist>(R &amp;&amp;paths)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>flags</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>ae792cc7777f4ddc21b0ea109e6a29762</anchor>
      <arglist>(std::string_view raw)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>linker_flags</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a1baaa5fa57d209ab516f021342f4cbb8</anchor>
      <arglist>(std::string_view raw)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>cpp_standard</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>ae346f7b028623ffdcc0326b27eaad10f</anchor>
      <arglist>(CppStandard s)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>warnings</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a9c1e680e2efbfbdb605ea793578b6dff</anchor>
      <arglist>(Warnings w)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>debug_info</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a184559ee387b27f41cced324451d37e6</anchor>
      <arglist>(DebugInfo d)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>optimization</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>ac6f83052eb91beba5511f21050fb340f</anchor>
      <arglist>(Optimization o)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>sanitizer</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a5926e5348333167d7cbbebf2c68bf54d</anchor>
      <arglist>(Sanitizer s)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>lto</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a57a8e5d668e6b48508a5a3e58e0f5fc5</anchor>
      <arglist>(Lto l)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>harden</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a61c7506131b11225f2b24b2d61805629</anchor>
      <arglist>(Hardening h)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>pch</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>af6e64b9c8940fa20874dd0595394df01</anchor>
      <arglist>(std::string_view path)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>unity</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a9d948e3652abee4c409876156c2673ec</anchor>
      <arglist>(bool v=true)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>modules_mode</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>ab39092526878f594ea83aae156ad98e8</anchor>
      <arglist>(ModulesMode m)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>pic</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a070e43f58ffbfceec1d5fd16bd269050</anchor>
      <arglist>(bool v)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>cross_target</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a20d843c212ec397504b831c0f21377e9</anchor>
      <arglist>(TargetTriple t)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>depends_on</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>aa29e6ed4d2c4f31c24fc6ec80fb9d4d3</anchor>
      <arglist>(const Target &amp;dep)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>depends_on</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a2927780a6a53762520be1cf50a741f45</anchor>
      <arglist>(Result&lt; TargetRef &gt; dep_result)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>is_test</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>aa5d12b20beae077c1dbc468335e9742c</anchor>
      <arglist>(bool v=true)</arglist>
    </member>
    <member kind="function">
      <type>const std::string &amp;</type>
      <name>name</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>aa76e85d15650e51d506053248f7182bc</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>TargetKind</type>
      <name>kind</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a79be5d1622b6db4ce9e356d17c6fff15</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::vector&lt; std::string &gt; &amp;</type>
      <name>source_patterns</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a2f1107019389d8b271010ac033457510</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::vector&lt; std::string &gt; &amp;</type>
      <name>removed_sources</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>afe74716613cd5644e1e9c623c45e9588</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::vector&lt; std::string &gt; &amp;</type>
      <name>include_dirs</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a3163a9fca3e75a1e8c2db8fb6c857490</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::vector&lt; std::string &gt; &amp;</type>
      <name>defines</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>aa27f30a3ee12e5a7fb9691bdf502a44d</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::vector&lt; std::string &gt; &amp;</type>
      <name>link_libs</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a4d5672671e9e449f8b54fb0f263a0aa1</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::vector&lt; std::string &gt; &amp;</type>
      <name>link_paths</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a80fe218254a1a34a42a94c9ab419c1f5</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::vector&lt; std::string &gt; &amp;</type>
      <name>frameworks</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>ae54e6339baefc74b364d5835ce73d785</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::vector&lt; std::string &gt; &amp;</type>
      <name>framework_paths</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>ab9c302e0712f050dbe340a418018be3c</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::string &amp;</type>
      <name>flags</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a1158fe19e95345e77cef56f022c7ae82</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::string &amp;</type>
      <name>linker_flags</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a845d3f5eb2183b0c6ef5cb2c16fa4035</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>CppStandard</type>
      <name>cpp_standard</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a571d3ec29a137d624bfbec9631b41611</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>Warnings</type>
      <name>warnings</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>afc3b8c38780e4a12757c195ff7180cd1</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>DebugInfo</type>
      <name>debug_info</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a4730ee466d743e09ae9967fbb0b0e9f9</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>Optimization</type>
      <name>optimization</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a07c725354af66d2667c64b6e041771a4</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>Sanitizer</type>
      <name>sanitizer</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>ae5d3f9478ec21978899cdc3be3c49725</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>Lto</type>
      <name>lto</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>ad64222fd0ee07769b2c4f15f507c5905</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>Hardening</type>
      <name>hardening</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a0205e8e0c54e7513026a0c3c25c5939d</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::string &amp;</type>
      <name>pch</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a7fb6ac18e2c5dbd1b590f62d3fd56966</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>bool</type>
      <name>unity</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a946099b5872f2b358082b005ce4f2e6e</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>ModulesMode</type>
      <name>modules_mode</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>aecc4f37bc29eb3aa9005fabb45b2a9fe</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>bool</type>
      <name>pic</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>ab0873bb41226b68dc1e283152f5680d7</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::optional&lt; TargetTriple &gt; &amp;</type>
      <name>triple_override</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a86c44098af17b66ceef42c34c9d3a475</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>const std::vector&lt; const Target * &gt; &amp;</type>
      <name>deps</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>ac4db94026bc4decd2bc64015b43e791d</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>bool</type>
      <name>is_test_target</name>
      <anchorfile>classtruss_1_1Target.html</anchorfile>
      <anchor>a45ee544c600929e4e5b0bd2022a53546</anchor>
      <arglist>() const</arglist>
    </member>
  </compound>
  <compound kind="class">
    <name>truss::TargetRef</name>
    <filename>classtruss_1_1TargetRef.html</filename>
    <member kind="function">
      <type></type>
      <name>TargetRef</name>
      <anchorfile>classtruss_1_1TargetRef.html</anchorfile>
      <anchor>a7d93395853823fb5e448f22a2df47a0c</anchor>
      <arglist>(Target &amp;t)</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>get</name>
      <anchorfile>classtruss_1_1TargetRef.html</anchorfile>
      <anchor>a299d7f5fd35887cbbbd260b2078f3a8d</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>Target &amp;</type>
      <name>operator*</name>
      <anchorfile>classtruss_1_1TargetRef.html</anchorfile>
      <anchor>a682062690c01aaf71a46fe082915ec44</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>Target *</type>
      <name>operator-&gt;</name>
      <anchorfile>classtruss_1_1TargetRef.html</anchorfile>
      <anchor>af8fc78af42093fa9c0784ff4589210fe</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type></type>
      <name>operator Target &amp;</name>
      <anchorfile>classtruss_1_1TargetRef.html</anchorfile>
      <anchor>a9d00d04c479433f5de283b777ea122f5</anchor>
      <arglist>() const</arglist>
    </member>
  </compound>
  <compound kind="struct">
    <name>truss::TargetTriple</name>
    <filename>structtruss_1_1TargetTriple.html</filename>
    <member kind="variable">
      <type>Os</type>
      <name>os</name>
      <anchorfile>structtruss_1_1TargetTriple.html</anchorfile>
      <anchor>a9530032e56c6e93106b7e259628a7c4f</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>Arch</type>
      <name>arch</name>
      <anchorfile>structtruss_1_1TargetTriple.html</anchorfile>
      <anchor>a339c81d94aa9007df43a04f8dc464fab</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>CompilerFamily</type>
      <name>family</name>
      <anchorfile>structtruss_1_1TargetTriple.html</anchorfile>
      <anchor>ae3f58c90aeb40fed5e0db9d2aae018d5</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::string</type>
      <name>compiler</name>
      <anchorfile>structtruss_1_1TargetTriple.html</anchorfile>
      <anchor>afa7c6767ce199dd3c6a72d66c51473c7</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::string</type>
      <name>ar</name>
      <anchorfile>structtruss_1_1TargetTriple.html</anchorfile>
      <anchor>a99ca47b699dd0b5a86305d22917381da</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::string</type>
      <name>linker</name>
      <anchorfile>structtruss_1_1TargetTriple.html</anchorfile>
      <anchor>a6d7d02e47baec38acf5a5b68bafd8525</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>StdLib</type>
      <name>stdlib</name>
      <anchorfile>structtruss_1_1TargetTriple.html</anchorfile>
      <anchor>a6d41a0851bf1c3ea4cda2a6e88dc427c</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="struct">
    <name>truss::TaskContext</name>
    <filename>structtruss_1_1TaskContext.html</filename>
    <member kind="variable">
      <type>std::string</type>
      <name>project</name>
      <anchorfile>structtruss_1_1TaskContext.html</anchorfile>
      <anchor>a4eaa7cd81283910b1fe7b3cd87efed14</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>BuildMode</type>
      <name>mode</name>
      <anchorfile>structtruss_1_1TaskContext.html</anchorfile>
      <anchor>a68e994b715149bff65729b4d21437dcf</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::filesystem::path</type>
      <name>build_dir</name>
      <anchorfile>structtruss_1_1TaskContext.html</anchorfile>
      <anchor>ac79e3d7fc3037d1255cb6283c90450a1</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>std::filesystem::path</type>
      <name>cwd</name>
      <anchorfile>structtruss_1_1TaskContext.html</anchorfile>
      <anchor>a230f7cb4a7eb90f84b53e3f6039d4df9</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>int</type>
      <name>jobs</name>
      <anchorfile>structtruss_1_1TaskContext.html</anchorfile>
      <anchor>af64136cb6f55e7e885403bd2ea229893</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>bool</type>
      <name>verbose</name>
      <anchorfile>structtruss_1_1TaskContext.html</anchorfile>
      <anchor>a8f50cd5da294c836487e024484760ee1</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="class">
    <name>truss::os::temp_scope</name>
    <filename>classtruss_1_1os_1_1temp__scope.html</filename>
    <member kind="function">
      <type></type>
      <name>temp_scope</name>
      <anchorfile>classtruss_1_1os_1_1temp__scope.html</anchorfile>
      <anchor>a0340d5b4c9a92bd4c7381517d222eefd</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type></type>
      <name>temp_scope</name>
      <anchorfile>classtruss_1_1os_1_1temp__scope.html</anchorfile>
      <anchor>af1801802221b1f137d25a54b7934d391</anchor>
      <arglist>(const temp_scope &amp;)=delete</arglist>
    </member>
    <member kind="function">
      <type>temp_scope &amp;</type>
      <name>operator=</name>
      <anchorfile>classtruss_1_1os_1_1temp__scope.html</anchorfile>
      <anchor>ae41cf9cba9da96e0c50ba5ef56e238b7</anchor>
      <arglist>(const temp_scope &amp;)=delete</arglist>
    </member>
    <member kind="function">
      <type></type>
      <name>temp_scope</name>
      <anchorfile>classtruss_1_1os_1_1temp__scope.html</anchorfile>
      <anchor>a1a78b7a3fe5fd067a6d38d38036a9d37</anchor>
      <arglist>(temp_scope &amp;&amp;) noexcept</arglist>
    </member>
    <member kind="function">
      <type>temp_scope &amp;</type>
      <name>operator=</name>
      <anchorfile>classtruss_1_1os_1_1temp__scope.html</anchorfile>
      <anchor>ab8deb0fb06cbe96f0ce16e2ea80bdc17</anchor>
      <arglist>(temp_scope &amp;&amp;) noexcept</arglist>
    </member>
    <member kind="function">
      <type></type>
      <name>~temp_scope</name>
      <anchorfile>classtruss_1_1os_1_1temp__scope.html</anchorfile>
      <anchor>a5d95cc37e9152640a96bab74eb6a356b</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>const std::filesystem::path &amp;</type>
      <name>path</name>
      <anchorfile>classtruss_1_1os_1_1temp__scope.html</anchorfile>
      <anchor>ae6df2d12e33076fe7e48f1b4f4772fb9</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>bool</type>
      <name>valid</name>
      <anchorfile>classtruss_1_1os_1_1temp__scope.html</anchorfile>
      <anchor>a20397dba4a4bbecde5c7ad34e5f694b8</anchor>
      <arglist>() const</arglist>
    </member>
    <member kind="function">
      <type>void</type>
      <name>release</name>
      <anchorfile>classtruss_1_1os_1_1temp__scope.html</anchorfile>
      <anchor>a0fb6d4d375796ea78bdde60d5a0799a1</anchor>
      <arglist>()</arglist>
    </member>
  </compound>
  <compound kind="struct">
    <name>truss::fs::WalkOptions</name>
    <filename>structtruss_1_1fs_1_1WalkOptions.html</filename>
    <member kind="variable">
      <type>int</type>
      <name>depth</name>
      <anchorfile>structtruss_1_1fs_1_1WalkOptions.html</anchorfile>
      <anchor>a1312dc6b7b01ae66fb2e3567175afdff</anchor>
      <arglist></arglist>
    </member>
    <member kind="variable">
      <type>bool</type>
      <name>skip_hidden</name>
      <anchorfile>structtruss_1_1fs_1_1WalkOptions.html</anchorfile>
      <anchor>a7398949f1631ba0e8f8ef7d45b476c1e</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="concept">
    <name>truss::os::EnvParsable</name>
    <filename>concepttruss_1_1os_1_1EnvParsable.html</filename>
  </compound>
  <compound kind="concept">
    <name>truss::StringRange</name>
    <filename>concepttruss_1_1StringRange.html</filename>
  </compound>
  <compound kind="namespace">
    <name>truss</name>
    <filename>namespacetruss.html</filename>
    <namespace>truss::detail</namespace>
    <namespace>truss::fs</namespace>
    <namespace>truss::log</namespace>
    <namespace>truss::os</namespace>
    <class kind="struct">truss::FetchOptions</class>
    <class kind="struct">truss::Package</class>
    <class kind="struct">truss::Error</class>
    <class kind="class">truss::generator</class>
    <class kind="struct">truss::TargetTriple</class>
    <class kind="class">truss::Config</class>
    <class kind="class">truss::Target</class>
    <class kind="class">truss::Executable</class>
    <class kind="class">truss::StaticLib</class>
    <class kind="class">truss::SharedLib</class>
    <class kind="class">truss::Interface</class>
    <class kind="class">truss::TargetRef</class>
    <class kind="struct">truss::TaskContext</class>
    <class kind="class">truss::Project</class>
    <concept>truss::StringRange</concept>
    <member kind="typedef">
      <type>std::expected&lt; T, Error &gt;</type>
      <name>Result</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>a89efd8655d412e1ac067dfe05ce1d15d</anchor>
      <arglist></arglist>
    </member>
    <member kind="typedef">
      <type>std::function&lt; Result&lt; void &gt;(const TaskContext &amp;)&gt;</type>
      <name>TaskFn</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>a32c053cd9de29c2d43b3c0647fb9e329</anchor>
      <arglist></arglist>
    </member>
    <member kind="enumeration">
      <type></type>
      <name>Os</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>a33b0047dae8b8782c167eb8aa87f43c2</anchor>
      <arglist></arglist>
      <enumvalue file="namespacetruss.html" anchor="a33b0047dae8b8782c167eb8aa87f43c2a88183b946cc5f0e8c96b2e66e1c74a7e">Unknown</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a33b0047dae8b8782c167eb8aa87f43c2aedc9f0a5a5d57797bf68e37364743831">Linux</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a33b0047dae8b8782c167eb8aa87f43c2aaea23489ce3aa9b6406ebb28e0cda430">Windows</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a33b0047dae8b8782c167eb8aa87f43c2a0a5b7edb55b772c60bfa8af868b679cf">MacOS</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a33b0047dae8b8782c167eb8aa87f43c2aa3dc630729e463135f4e608954fa6e19">FreeBSD</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a33b0047dae8b8782c167eb8aa87f43c2ae84e30b9390cdb64db6db2c9ab87846d">Android</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a33b0047dae8b8782c167eb8aa87f43c2a680890ec84706d822adabcc4f3ea2376">Emscripten</enumvalue>
    </member>
    <member kind="enumeration">
      <type></type>
      <name>Arch</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>aca2d8947359bb7f270c145313eed7dfa</anchor>
      <arglist></arglist>
      <enumvalue file="namespacetruss.html" anchor="aca2d8947359bb7f270c145313eed7dfaa88183b946cc5f0e8c96b2e66e1c74a7e">Unknown</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="aca2d8947359bb7f270c145313eed7dfaaf0851da0e02bf22830828822f578dc8f">X64</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="aca2d8947359bb7f270c145313eed7dfaafd1a4608b5b463bc7a41d86a1662d3b7">X86</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="aca2d8947359bb7f270c145313eed7dfaac42c3b5bc3b4234ee7823388012368d6">Arm64</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="aca2d8947359bb7f270c145313eed7dfaa044fc9e908d0f160ae1a8ac300eb2b3d">Arm32</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="aca2d8947359bb7f270c145313eed7dfaaf11b98e4b7fb16dd40525e7b3bacd336">Riscv64</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="aca2d8947359bb7f270c145313eed7dfaab06ff2fdc13edfde38273f8b0d8ab663">Ppc64</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="aca2d8947359bb7f270c145313eed7dfaa4fee4b081eaa38e4ecb3fecf712e6e62">S390x</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="aca2d8947359bb7f270c145313eed7dfaaafac814bd893ab22c9bd07459cbad436">Wasm32</enumvalue>
    </member>
    <member kind="enumeration">
      <type></type>
      <name>CompilerFamily</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>aa8f3754c08d3a98cbe7ab7e3d70047ac</anchor>
      <arglist></arglist>
      <enumvalue file="namespacetruss.html" anchor="aa8f3754c08d3a98cbe7ab7e3d70047aca88183b946cc5f0e8c96b2e66e1c74a7e">Unknown</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="aa8f3754c08d3a98cbe7ab7e3d70047acaedfa57e7c7b4dd9a9d4a55087e5ac1a9">Gcc</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="aa8f3754c08d3a98cbe7ab7e3d70047aca9375884cf4ed31c834a626c324dddc67">Clang</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="aa8f3754c08d3a98cbe7ab7e3d70047acad39184b28266faaf10329534d8b07c07">Msvc</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="aa8f3754c08d3a98cbe7ab7e3d70047acae8127ec3b0d9c6c942274ab4339212e4">Tcc</enumvalue>
    </member>
    <member kind="enumeration">
      <type></type>
      <name>BuildMode</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>ab9335542b2978631928af94deaff5767</anchor>
      <arglist></arglist>
      <enumvalue file="namespacetruss.html" anchor="ab9335542b2978631928af94deaff5767aa603905470e2a5b8c13e96b579ef0dba">Debug</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="ab9335542b2978631928af94deaff5767ab8e7b465df7c5979dc731d06e84ce2cf">Release</enumvalue>
    </member>
    <member kind="enumeration">
      <type></type>
      <name>CppStandard</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>a5d63ccf36e18288095c774a4fa0baaad</anchor>
      <arglist></arglist>
      <enumvalue file="namespacetruss.html" anchor="a5d63ccf36e18288095c774a4fa0baaada7a1920d61156abc05a60135aefe8bc67">Default</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a5d63ccf36e18288095c774a4fa0baaada476c9418fce7a7906aecc9c84b0f621b">Cpp11</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a5d63ccf36e18288095c774a4fa0baaada71ee3a1e3837e0e6de09b87d586806df">Cpp14</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a5d63ccf36e18288095c774a4fa0baaada8491a119aa343387ef493b97ee4aa3cd">Cpp17</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a5d63ccf36e18288095c774a4fa0baaada6158e610883ad77109010b1da97ae706">Cpp20</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a5d63ccf36e18288095c774a4fa0baaada2018534533e86696517616e4e4d7a959">Cpp23</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a5d63ccf36e18288095c774a4fa0baaada0a3dbdb0642f6e07206b16e45a6359b5">Cpp26</enumvalue>
    </member>
    <member kind="enumeration">
      <type></type>
      <name>Warnings</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>a766c843cc3f0c7e511d3df9311e4425f</anchor>
      <arglist></arglist>
      <enumvalue file="namespacetruss.html" anchor="a766c843cc3f0c7e511d3df9311e4425fa7a1920d61156abc05a60135aefe8bc67">Default</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a766c843cc3f0c7e511d3df9311e4425fa6adf97f83acf6453d4a6a4b1070f3754">None</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a766c843cc3f0c7e511d3df9311e4425fa30fc6bbba82125243ecf4ddb27fee645">Minimal</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a766c843cc3f0c7e511d3df9311e4425fab1c94ca2fbc3e78fc30069c8d0f01680">All</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a766c843cc3f0c7e511d3df9311e4425fab12a5bededfa7b797367002ec57b1e0e">Pedantic</enumvalue>
    </member>
    <member kind="enumeration">
      <type></type>
      <name>DebugInfo</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>af62ead86d0b55c8df3022f665df189af</anchor>
      <arglist></arglist>
      <enumvalue file="namespacetruss.html" anchor="af62ead86d0b55c8df3022f665df189afa7a1920d61156abc05a60135aefe8bc67">Default</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="af62ead86d0b55c8df3022f665df189afa6adf97f83acf6453d4a6a4b1070f3754">None</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="af62ead86d0b55c8df3022f665df189afa30fc6bbba82125243ecf4ddb27fee645">Minimal</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="af62ead86d0b55c8df3022f665df189afa87f8a6ab85c9ced3702b4ea641ad4bb5">Medium</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="af62ead86d0b55c8df3022f665df189afabbd47109890259c0127154db1af26c75">Full</enumvalue>
    </member>
    <member kind="enumeration">
      <type></type>
      <name>Optimization</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>adce936dd79df0fae17512c3c033289b0</anchor>
      <arglist></arglist>
      <enumvalue file="namespacetruss.html" anchor="adce936dd79df0fae17512c3c033289b0a7a1920d61156abc05a60135aefe8bc67">Default</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="adce936dd79df0fae17512c3c033289b0a6adf97f83acf6453d4a6a4b1070f3754">None</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="adce936dd79df0fae17512c3c033289b0a00594a175ce5a58f286d91ca0a6f15a2">O1</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="adce936dd79df0fae17512c3c033289b0abfc60714f62f15e1e72e0b21454cc99e">O2</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="adce936dd79df0fae17512c3c033289b0a6f6cb72d544962fa333e2e34ce64f719">Size</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="adce936dd79df0fae17512c3c033289b0a9ee25ee8b4d0a573527fcd2f1e73f8c8">O3</enumvalue>
    </member>
    <member kind="enumeration">
      <type></type>
      <name>Sanitizer</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>ac816149df3a72b103e6b9fd740e8fa63</anchor>
      <arglist></arglist>
      <enumvalue file="namespacetruss.html" anchor="ac816149df3a72b103e6b9fd740e8fa63a7a1920d61156abc05a60135aefe8bc67">Default</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="ac816149df3a72b103e6b9fd740e8fa63a6adf97f83acf6453d4a6a4b1070f3754">None</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="ac816149df3a72b103e6b9fd740e8fa63add7bf230fde8d4836917806aff6a6b27">Address</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="ac816149df3a72b103e6b9fd740e8fa63a43e14e6052c71fd972a872befaf6af66">Ub</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="ac816149df3a72b103e6b9fd740e8fa63ab1c94ca2fbc3e78fc30069c8d0f01680">All</enumvalue>
    </member>
    <member kind="enumeration">
      <type></type>
      <name>Lto</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>af01b8742b79ea2787d4a8bd2f0cacc4f</anchor>
      <arglist></arglist>
      <enumvalue file="namespacetruss.html" anchor="af01b8742b79ea2787d4a8bd2f0cacc4fa7a1920d61156abc05a60135aefe8bc67">Default</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="af01b8742b79ea2787d4a8bd2f0cacc4fa6adf97f83acf6453d4a6a4b1070f3754">None</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="af01b8742b79ea2787d4a8bd2f0cacc4fabbd47109890259c0127154db1af26c75">Full</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="af01b8742b79ea2787d4a8bd2f0cacc4faaedfcbf06fc92bafadf4527a2fcccc69">Thin</enumvalue>
    </member>
    <member kind="enumeration">
      <type></type>
      <name>Hardening</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>adbbb8fe1927d9782ddf6aa3987580a1c</anchor>
      <arglist></arglist>
      <enumvalue file="namespacetruss.html" anchor="adbbb8fe1927d9782ddf6aa3987580a1ca7a1920d61156abc05a60135aefe8bc67">Default</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="adbbb8fe1927d9782ddf6aa3987580a1ca6adf97f83acf6453d4a6a4b1070f3754">None</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="adbbb8fe1927d9782ddf6aa3987580a1ca9914a0ce04a7b7b6a8e39bec55064b82">Light</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="adbbb8fe1927d9782ddf6aa3987580a1ca87f8a6ab85c9ced3702b4ea641ad4bb5">Medium</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="adbbb8fe1927d9782ddf6aa3987580a1ca6a061313d22e51e0f25b7cd4dc065233">Max</enumvalue>
    </member>
    <member kind="enumeration">
      <type></type>
      <name>ModulesMode</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>a231085c8ba46482da3f44af3b5f8a880</anchor>
      <arglist></arglist>
      <enumvalue file="namespacetruss.html" anchor="a231085c8ba46482da3f44af3b5f8a880a7a1920d61156abc05a60135aefe8bc67">Default</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a231085c8ba46482da3f44af3b5f8a880ad15305d7a4e34e02489c74a5ef542f36">Off</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a231085c8ba46482da3f44af3b5f8a880a06b9281e396db002010bde1de57262eb">Auto</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="a231085c8ba46482da3f44af3b5f8a880ae1ba155a9f2e8c3be94020eef32a0301">Manual</enumvalue>
    </member>
    <member kind="enumeration">
      <type></type>
      <name>StdLib</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>ac9c289f1135e170aa2b0ecf66d9297a5</anchor>
      <arglist></arglist>
      <enumvalue file="namespacetruss.html" anchor="ac9c289f1135e170aa2b0ecf66d9297a5a7a1920d61156abc05a60135aefe8bc67">Default</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="ac9c289f1135e170aa2b0ecf66d9297a5a003db3efa4d5483a66577a19ce2c31cd">Libstdcxx</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="ac9c289f1135e170aa2b0ecf66d9297a5a87a7d51bbd3d4079f0e04f0e0b1b899a">Libcxx</enumvalue>
    </member>
    <member kind="enumeration">
      <type></type>
      <name>Toolchain</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>aca1bd587f3c8e9ad2bcacd4b0c29b5d9</anchor>
      <arglist></arglist>
      <enumvalue file="namespacetruss.html" anchor="aca1bd587f3c8e9ad2bcacd4b0c29b5d9a7a1920d61156abc05a60135aefe8bc67">Default</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="aca1bd587f3c8e9ad2bcacd4b0c29b5d9a66a0128ad08b2053e4809e07fe05728c">LLVM</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="aca1bd587f3c8e9ad2bcacd4b0c29b5d9a32d8b233e3c58a262a0b75872297d8d5">GCC</enumvalue>
    </member>
    <member kind="enumeration">
      <type></type>
      <name>TargetKind</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>ae06869e58246c1e45c36fc99ecf28ecf</anchor>
      <arglist></arglist>
      <enumvalue file="namespacetruss.html" anchor="ae06869e58246c1e45c36fc99ecf28ecfa38492e6b60e40c59a376e2151778e2c7">Executable</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="ae06869e58246c1e45c36fc99ecf28ecfad65ac3c20c2779c34086c196f241e2af">StaticLibrary</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="ae06869e58246c1e45c36fc99ecf28ecfa8988989e16522c86d49619bbd7a09bd8">SharedLibrary</enumvalue>
      <enumvalue file="namespacetruss.html" anchor="ae06869e58246c1e45c36fc99ecf28ecfa3c1aac82863ed9e5a9aca8ce687f711d">Interface</enumvalue>
    </member>
    <member kind="function">
      <type>TargetTriple</type>
      <name>host_triple</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>a40df9db897af63ab8eb7b320b138b5cb</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="variable">
      <type>constexpr std::string_view</type>
      <name>kVersion</name>
      <anchorfile>namespacetruss.html</anchorfile>
      <anchor>a8a37d2c718fea1edb3c0d20b5c85017b</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="namespace">
    <name>truss::detail</name>
    <filename>namespacetruss_1_1detail.html</filename>
    <class kind="struct">truss::detail::ProcOutcome</class>
    <class kind="struct">truss::detail::ModuleUnitInfo</class>
    <class kind="struct">truss::detail::FlagSet</class>
    <class kind="struct">truss::detail::LinkFlags</class>
    <class kind="class">truss::detail::NinjaWriter</class>
    <member kind="function">
      <type>Result&lt; std::string &gt;</type>
      <name>read_file</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga370c47f126a035faac6b838b006bc582</anchor>
      <arglist>(const std::filesystem::path &amp;p)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>write_file</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga1997ed3e7f93711984a2d8b0df21af83</anchor>
      <arglist>(const std::filesystem::path &amp;p, std::string_view data)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>write_file_if_changed</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga74662d43ad58bc01aaaf730af3cfb0d5</anchor>
      <arglist>(const std::filesystem::path &amp;p, std::string_view data)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>make_dirs</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>gabece1396d7f65a4952395dea18d55611</anchor>
      <arglist>(const std::filesystem::path &amp;p)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>remove_tree</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga02d13f8c3359853473b4d56d486e00d5</anchor>
      <arglist>(const std::filesystem::path &amp;p)</arglist>
    </member>
    <member kind="function">
      <type>std::map&lt; std::string, std::string &gt;</type>
      <name>read_lockfile</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga517b219f9f49b55ef6ac84e2f197b749</anchor>
      <arglist>(const std::filesystem::path &amp;file)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>write_lockfile</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga4c4051fae3f7d97ce98fef85dc6f08a5</anchor>
      <arglist>(const std::filesystem::path &amp;file, const std::map&lt; std::string, std::string &gt; &amp;entries)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; std::vector&lt; std::filesystem::path &gt; &gt;</type>
      <name>glob</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga874c4a14c8afc777e6dd21ae930100fd</anchor>
      <arglist>(std::string_view pattern)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; std::vector&lt; std::filesystem::path &gt; &gt;</type>
      <name>glob_multi</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga748ab2668de19b0e784ef85a772e13b2</anchor>
      <arglist>(std::span&lt; const std::string &gt; patterns)</arglist>
    </member>
    <member kind="function">
      <type>bool</type>
      <name>has_glob_magic</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga43b0ff1ccd89b3e9e24abfc446605727</anchor>
      <arglist>(std::string_view pattern)</arglist>
    </member>
    <member kind="function">
      <type>std::string</type>
      <name>path_str</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga3542187be25abd3acabb1380012ea51e</anchor>
      <arglist>(const std::filesystem::path &amp;p)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; ProcOutcome &gt;</type>
      <name>run_process</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga6010dc5a2e854347b82fbaeb02012828</anchor>
      <arglist>(std::span&lt; const std::string &gt; argv, bool capture)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; ProcOutcome &gt;</type>
      <name>run_process</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>gac765fb755eb638b713b1397606c69bf7</anchor>
      <arglist>(std::span&lt; const std::string &gt; argv, bool capture, const std::filesystem::path &amp;cwd)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; ProcOutcome &gt;</type>
      <name>run_process_stdout</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>gabb1ed38d1ae857dcc21f0f7bd01fd2b8</anchor>
      <arglist>(std::span&lt; const std::string &gt; argv)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; ModuleUnitInfo &gt;</type>
      <name>scan_module_unit</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga1e585d0c02c1924c0598575c4c081890</anchor>
      <arglist>(const std::string &amp;path)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; std::pair&lt; std::string, std::vector&lt; std::string &gt; &gt; &gt;</type>
      <name>resolve_module_imports</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga9a359b1ac90a16e9fe5276191b3e1c98</anchor>
      <arglist>(const ModuleUnitInfo &amp;u, const std::map&lt; std::string, std::string, std::less&lt;&gt; &gt; &amp;bmis)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; std::vector&lt; std::string &gt; &gt;</type>
      <name>scan_deps_requires</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga23920b2cd9f0f51129c9baa6693a18d8</anchor>
      <arglist>(const std::string &amp;compiler, std::string_view compile_flags, const std::string &amp;source, const std::filesystem::path &amp;cwd, const std::filesystem::path &amp;scratch)</arglist>
    </member>
    <member kind="function">
      <type>DebugInfo</type>
      <name>resolve_debug</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga750054f916470831630f548a2650dcf1</anchor>
      <arglist>(BuildMode mode, DebugInfo debug)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; FlagSet &gt;</type>
      <name>build_flags</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga13e88dd52e713b2413cc2ff1b4ce817b</anchor>
      <arglist>(const TargetTriple &amp;triple, BuildMode mode, CppStandard std, Warnings warnings, DebugInfo debug, Optimization optimization, Sanitizer sanitizer, Lto lto, Hardening hardening, bool pic, std::span&lt; const std::string &gt; defines, std::span&lt; const std::string &gt; includes, std::string_view user_flags)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; LinkFlags &gt;</type>
      <name>link_flags</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga4c833b629d0c0dbd040436392ea44119</anchor>
      <arglist>(const TargetTriple &amp;triple, BuildMode mode, DebugInfo debug, Sanitizer sanitizer, Lto lto, Hardening hardening, std::span&lt; const std::string &gt; link_paths, std::span&lt; const std::string &gt; libs, std::span&lt; const std::string &gt; frameworks, std::span&lt; const std::string &gt; framework_paths, std::string_view user_linker_flags)</arglist>
    </member>
    <member kind="function">
      <type>std::string</type>
      <name>triple_dir</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>gaedaa53fc693dff9d5becdbf41e6bf2b0</anchor>
      <arglist>(const TargetTriple &amp;t)</arglist>
    </member>
    <member kind="function">
      <type>CompilerFamily</type>
      <name>family_from_compiler_name</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>gafcc172548392de88d41faf4427056979</anchor>
      <arglist>(std::string_view name)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; TargetTriple &gt;</type>
      <name>apply_config</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga5bc696bd0bc1a0aa6be87e7e2972aa5a</anchor>
      <arglist>(const Config &amp;conf, const TargetTriple &amp;base)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; std::string &gt;</type>
      <name>resolve_launcher</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>gaa41e0d980cd7fa5d226fd22e031a85d8</anchor>
      <arglist>(const Config &amp;conf)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>copy_tree</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga8f4fbbbca03cb6fb3896881b09976675</anchor>
      <arglist>(const std::filesystem::path &amp;from, const std::filesystem::path &amp;to)</arglist>
    </member>
    <member kind="function">
      <type>std::string</type>
      <name>esc_value</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>gaa41d5db9cd0f627411024f6b1b1b310d</anchor>
      <arglist>(std::string_view v)</arglist>
    </member>
    <member kind="function">
      <type>std::string</type>
      <name>esc_path</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga1aef11ad00aa13c8a7408fb01f69e087</anchor>
      <arglist>(std::string_view p)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>samu_build</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga183f6507b11b7ad74131e5396e19e9ed</anchor>
      <arglist>(std::string_view manifest, int jobs, bool verbose, bool dry_run, bool explain, std::span&lt; const std::string &gt; targets)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>samu_compdb</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga4f664ae39cf7b593f94c7f1d25754f2a</anchor>
      <arglist>(std::string_view manifest, std::string_view outpath)</arglist>
    </member>
  </compound>
  <compound kind="namespace">
    <name>truss::fs</name>
    <filename>namespacetruss_1_1fs.html</filename>
    <class kind="struct">truss::fs::WalkOptions</class>
    <member kind="function">
      <type>Result&lt; std::vector&lt; std::filesystem::path &gt; &gt;</type>
      <name>glob</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>af0b4897d969a320a7c75d03e62aca7cd</anchor>
      <arglist>(std::string_view pattern)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; std::vector&lt; std::filesystem::path &gt; &gt;</type>
      <name>glob</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>aec6efbebe220102ad2175bde44eecdcf</anchor>
      <arglist>(std::initializer_list&lt; std::string_view &gt; patterns)</arglist>
    </member>
    <member kind="function">
      <type>generator&lt; std::filesystem::path &gt;</type>
      <name>walk</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>aedf03c9e6d30f8c249deee284c04eded</anchor>
      <arglist>(const std::filesystem::path &amp;dir, WalkOptions opts={})</arglist>
    </member>
    <member kind="function">
      <type>std::optional&lt; std::filesystem::path &gt;</type>
      <name>find_upward</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>ab6247cdae8e631afaa7b93b4f17fb39b</anchor>
      <arglist>(std::string_view filename, const std::filesystem::path &amp;from_dir)</arglist>
    </member>
    <member kind="function">
      <type>std::optional&lt; std::filesystem::path &gt;</type>
      <name>find_upward</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>ad9a7be4f2835196980fa5ce1762af4ae</anchor>
      <arglist>(std::string_view filename)</arglist>
    </member>
    <member kind="function">
      <type>std::optional&lt; std::filesystem::path &gt;</type>
      <name>first_existing</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>acacb1dc4618ce1ea055214b81b1cc2d1</anchor>
      <arglist>(std::initializer_list&lt; std::filesystem::path &gt; candidates)</arglist>
    </member>
    <member kind="function">
      <type>std::optional&lt; std::filesystem::path &gt;</type>
      <name>find_library</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>a21bc114ff065eb076bc54bbf2a0def6e</anchor>
      <arglist>(std::string_view name, std::span&lt; const std::filesystem::path &gt; search_dirs)</arglist>
    </member>
    <member kind="function">
      <type>std::optional&lt; std::filesystem::path &gt;</type>
      <name>find_library</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>a9147b682bb964f725aa4713be045de66</anchor>
      <arglist>(std::string_view name)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; std::string &gt;</type>
      <name>read_text</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>a130ddd9aa28f1e0fadbb376d6e91bf32</anchor>
      <arglist>(const std::filesystem::path &amp;)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>write_text</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>abc4d32ec78173c8d372948cd1ccef1ed</anchor>
      <arglist>(const std::filesystem::path &amp;, std::string_view)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>write_text_if_changed</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>ae4c04f2cc10568ae49d79a6b5e056162</anchor>
      <arglist>(const std::filesystem::path &amp;, std::string_view)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; generator&lt; std::string &gt; &gt;</type>
      <name>read_lines</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>ab39fb70066a621b379a72694af66630e</anchor>
      <arglist>(const std::filesystem::path &amp;)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; std::string &gt;</type>
      <name>sha256_file</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>a1abe0a4405759088652bb1e1c83d4bda</anchor>
      <arglist>(const std::filesystem::path &amp;)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; std::filesystem::path &gt;</type>
      <name>current_dir</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>a5c1bb3bfff1bac9b047e25e9a5aa9515</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>copy_tree</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>a32dd79e1ffba29f726e6958ac44d7682</anchor>
      <arglist>(const std::filesystem::path &amp;from, const std::filesystem::path &amp;to)</arglist>
    </member>
    <member kind="function">
      <type>std::string</type>
      <name>generic_str</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>ae456d860525ea692e1b03c7742bc0b16</anchor>
      <arglist>(const std::filesystem::path &amp;)</arglist>
    </member>
    <member kind="variable">
      <type>constexpr int</type>
      <name>walk_unlimited</name>
      <anchorfile>namespacetruss_1_1fs.html</anchorfile>
      <anchor>ad72bc40ab20a84231471e5cf2e6c1113</anchor>
      <arglist></arglist>
    </member>
  </compound>
  <compound kind="namespace">
    <name>truss::log</name>
    <filename>namespacetruss_1_1log.html</filename>
    <member kind="function">
      <type>void</type>
      <name>info</name>
      <anchorfile>namespacetruss_1_1log.html</anchorfile>
      <anchor>a3ce4a0193d1430e011182863e306e47c</anchor>
      <arglist>(std::string_view msg)</arglist>
    </member>
    <member kind="function">
      <type>void</type>
      <name>warn</name>
      <anchorfile>namespacetruss_1_1log.html</anchorfile>
      <anchor>a3e1d21d580f7e7599d0d0262af402551</anchor>
      <arglist>(std::string_view msg)</arglist>
    </member>
    <member kind="function">
      <type>void</type>
      <name>success</name>
      <anchorfile>namespacetruss_1_1log.html</anchorfile>
      <anchor>af1a930ad6a8615b0125487119a463ba1</anchor>
      <arglist>(std::string_view msg)</arglist>
    </member>
    <member kind="function">
      <type>void</type>
      <name>error</name>
      <anchorfile>namespacetruss_1_1log.html</anchorfile>
      <anchor>aa76bf0a8ac768022a67f30e8060e9c56</anchor>
      <arglist>(std::string_view msg)</arglist>
    </member>
    <member kind="function">
      <type>void</type>
      <name>error</name>
      <anchorfile>namespacetruss_1_1log.html</anchorfile>
      <anchor>a10c2e38fbfe50c1113fe200f59833a5d</anchor>
      <arglist>(const Error &amp;err)</arglist>
    </member>
  </compound>
  <compound kind="namespace">
    <name>truss::os</name>
    <filename>namespacetruss_1_1os.html</filename>
    <class kind="struct">truss::os::ProcessResult</class>
    <class kind="struct">truss::os::RunOptions</class>
    <class kind="class">truss::os::temp_scope</class>
    <concept>truss::os::EnvParsable</concept>
    <member kind="function">
      <type>consteval Os</type>
      <name>host_os</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a60097f6115b376fdbf2a00e995777671</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>consteval Arch</type>
      <name>host_arch</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a62d670de9bb852eda091c966ec894dd1</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>consteval CompilerFamily</type>
      <name>host_family</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>ae2fd597f4b78b3409c4c2449f6c035b6</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>consteval bool</type>
      <name>is_linux</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a496ca982a4b042da2db60d189cbbe61c</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>consteval bool</type>
      <name>is_windows</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>aa4a1d162af58fa9ccb6a803c82ea7de3</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>consteval bool</type>
      <name>is_macos</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a71c99e543bad216658c0655b26ddfb55</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>std::optional&lt; std::filesystem::path &gt;</type>
      <name>find_program</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>ab7fe3e463a7402e59a37073e03f9188d</anchor>
      <arglist>(std::string_view name)</arglist>
    </member>
    <member kind="function">
      <type>std::optional&lt; std::filesystem::path &gt;</type>
      <name>find_program</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a3303cc9c13aba74335bad27a9c036bdf</anchor>
      <arglist>(const char *name)</arglist>
    </member>
    <member kind="function">
      <type>std::optional&lt; std::filesystem::path &gt;</type>
      <name>find_program</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a01a1e2063b76299fab030abe375761aa</anchor>
      <arglist>(std::initializer_list&lt; std::string_view &gt; names)</arglist>
    </member>
    <member kind="function">
      <type>bool</type>
      <name>program_exists</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a60b58d66acae3ee7b4ec2c61bb762d67</anchor>
      <arglist>(std::string_view name)</arglist>
    </member>
    <member kind="function">
      <type>bool</type>
      <name>all_programs</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>aaa4c555287a3a3c4bb8b97e861eb5a9e</anchor>
      <arglist>(std::initializer_list&lt; std::string_view &gt; names)</arglist>
    </member>
    <member kind="function">
      <type>bool</type>
      <name>any_program</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a4f8e9d19148a1db734360df7d05ed856</anchor>
      <arglist>(std::initializer_list&lt; std::string_view &gt; names)</arglist>
    </member>
    <member kind="function">
      <type>std::optional&lt; std::string &gt;</type>
      <name>env</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a327dbeb08d0fbdcb9c324970743cfe02</anchor>
      <arglist>(std::string_view name)</arglist>
    </member>
    <member kind="function">
      <type>std::optional&lt; std::string &gt;</type>
      <name>env</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>aff2dfc6e632f0428e38d2f78ca9d9cd4</anchor>
      <arglist>(const char *name)</arglist>
    </member>
    <member kind="function">
      <type>T</type>
      <name>env</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a69a8de32efeefd4292d6f3bbfefd5ddd</anchor>
      <arglist>(std::string_view name, T def)</arglist>
    </member>
    <member kind="function">
      <type>std::string</type>
      <name>env</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a3cf642b5f214b6bf177002791f429112</anchor>
      <arglist>(std::string_view name, const char *def)</arglist>
    </member>
    <member kind="function">
      <type>unsigned</type>
      <name>cpu_count</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a3cba357c50dd311af103500a2a3c449d</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>std::uint64_t</type>
      <name>memory_bytes</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a6473fa683f55a734e7fd60c7c33d1de8</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>std::optional&lt; std::string &gt;</type>
      <name>os_version</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a138036293b9afcbbbf90d0e4d09e5803</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>std::optional&lt; std::string &gt;</type>
      <name>hostname</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>aa4400729aa94a7a56da1c61316196fd0</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>std::filesystem::path</type>
      <name>temp_dir</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a0922026ea960179c64cec9a24e116e13</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>std::vector&lt; std::filesystem::path &gt;</type>
      <name>find_truss_modules</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>aacdd80bcc1c9c8052b01a2e5b5ed9c46</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; ProcessResult &gt;</type>
      <name>run</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a620a0ed9a7d008fe7802ec7f79ff15cc</anchor>
      <arglist>(std::span&lt; const std::string &gt; argv, RunOptions={})</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; ProcessResult &gt;</type>
      <name>capture</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a950772bda0b18eae32039587aeb85d7f</anchor>
      <arglist>(std::span&lt; const std::string &gt; argv, RunOptions={})</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; ProcessResult &gt;</type>
      <name>run</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a3cfc36a6b2eb5c6d95951e8ff301978e</anchor>
      <arglist>(std::initializer_list&lt; std::string &gt; argv, RunOptions opts={})</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; ProcessResult &gt;</type>
      <name>capture</name>
      <anchorfile>namespacetruss_1_1os.html</anchorfile>
      <anchor>a48231006a99a153430006fc23b974890</anchor>
      <arglist>(std::initializer_list&lt; std::string &gt; argv, RunOptions opts={})</arglist>
    </member>
  </compound>
  <compound kind="group">
    <name>grp_internals</name>
    <title>Internals</title>
    <filename>group__grp__internals.html</filename>
    <namespace>truss::detail</namespace>
    <class kind="struct">truss::detail::ProcOutcome</class>
    <class kind="struct">truss::detail::ModuleUnitInfo</class>
    <class kind="struct">truss::detail::FlagSet</class>
    <class kind="struct">truss::detail::LinkFlags</class>
    <class kind="class">truss::detail::NinjaWriter</class>
    <member kind="function">
      <type>Result&lt; std::string &gt;</type>
      <name>truss::detail::read_file</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga370c47f126a035faac6b838b006bc582</anchor>
      <arglist>(const std::filesystem::path &amp;p)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>truss::detail::write_file</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga1997ed3e7f93711984a2d8b0df21af83</anchor>
      <arglist>(const std::filesystem::path &amp;p, std::string_view data)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>truss::detail::write_file_if_changed</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga74662d43ad58bc01aaaf730af3cfb0d5</anchor>
      <arglist>(const std::filesystem::path &amp;p, std::string_view data)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>truss::detail::make_dirs</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>gabece1396d7f65a4952395dea18d55611</anchor>
      <arglist>(const std::filesystem::path &amp;p)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>truss::detail::remove_tree</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga02d13f8c3359853473b4d56d486e00d5</anchor>
      <arglist>(const std::filesystem::path &amp;p)</arglist>
    </member>
    <member kind="function">
      <type>std::map&lt; std::string, std::string &gt;</type>
      <name>truss::detail::read_lockfile</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga517b219f9f49b55ef6ac84e2f197b749</anchor>
      <arglist>(const std::filesystem::path &amp;file)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>truss::detail::write_lockfile</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga4c4051fae3f7d97ce98fef85dc6f08a5</anchor>
      <arglist>(const std::filesystem::path &amp;file, const std::map&lt; std::string, std::string &gt; &amp;entries)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; std::vector&lt; std::filesystem::path &gt; &gt;</type>
      <name>truss::detail::glob</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga874c4a14c8afc777e6dd21ae930100fd</anchor>
      <arglist>(std::string_view pattern)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; std::vector&lt; std::filesystem::path &gt; &gt;</type>
      <name>truss::detail::glob_multi</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga748ab2668de19b0e784ef85a772e13b2</anchor>
      <arglist>(std::span&lt; const std::string &gt; patterns)</arglist>
    </member>
    <member kind="function">
      <type>bool</type>
      <name>truss::detail::has_glob_magic</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga43b0ff1ccd89b3e9e24abfc446605727</anchor>
      <arglist>(std::string_view pattern)</arglist>
    </member>
    <member kind="function">
      <type>std::string</type>
      <name>truss::detail::path_str</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga3542187be25abd3acabb1380012ea51e</anchor>
      <arglist>(const std::filesystem::path &amp;p)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; ProcOutcome &gt;</type>
      <name>truss::detail::run_process</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga6010dc5a2e854347b82fbaeb02012828</anchor>
      <arglist>(std::span&lt; const std::string &gt; argv, bool capture)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; ProcOutcome &gt;</type>
      <name>truss::detail::run_process</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>gac765fb755eb638b713b1397606c69bf7</anchor>
      <arglist>(std::span&lt; const std::string &gt; argv, bool capture, const std::filesystem::path &amp;cwd)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; ProcOutcome &gt;</type>
      <name>truss::detail::run_process_stdout</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>gabb1ed38d1ae857dcc21f0f7bd01fd2b8</anchor>
      <arglist>(std::span&lt; const std::string &gt; argv)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; ModuleUnitInfo &gt;</type>
      <name>truss::detail::scan_module_unit</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga1e585d0c02c1924c0598575c4c081890</anchor>
      <arglist>(const std::string &amp;path)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; std::pair&lt; std::string, std::vector&lt; std::string &gt; &gt; &gt;</type>
      <name>truss::detail::resolve_module_imports</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga9a359b1ac90a16e9fe5276191b3e1c98</anchor>
      <arglist>(const ModuleUnitInfo &amp;u, const std::map&lt; std::string, std::string, std::less&lt;&gt; &gt; &amp;bmis)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; std::vector&lt; std::string &gt; &gt;</type>
      <name>truss::detail::scan_deps_requires</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga23920b2cd9f0f51129c9baa6693a18d8</anchor>
      <arglist>(const std::string &amp;compiler, std::string_view compile_flags, const std::string &amp;source, const std::filesystem::path &amp;cwd, const std::filesystem::path &amp;scratch)</arglist>
    </member>
    <member kind="function">
      <type>DebugInfo</type>
      <name>truss::detail::resolve_debug</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga750054f916470831630f548a2650dcf1</anchor>
      <arglist>(BuildMode mode, DebugInfo debug)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; FlagSet &gt;</type>
      <name>truss::detail::build_flags</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga13e88dd52e713b2413cc2ff1b4ce817b</anchor>
      <arglist>(const TargetTriple &amp;triple, BuildMode mode, CppStandard std, Warnings warnings, DebugInfo debug, Optimization optimization, Sanitizer sanitizer, Lto lto, Hardening hardening, bool pic, std::span&lt; const std::string &gt; defines, std::span&lt; const std::string &gt; includes, std::string_view user_flags)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; LinkFlags &gt;</type>
      <name>truss::detail::link_flags</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga4c833b629d0c0dbd040436392ea44119</anchor>
      <arglist>(const TargetTriple &amp;triple, BuildMode mode, DebugInfo debug, Sanitizer sanitizer, Lto lto, Hardening hardening, std::span&lt; const std::string &gt; link_paths, std::span&lt; const std::string &gt; libs, std::span&lt; const std::string &gt; frameworks, std::span&lt; const std::string &gt; framework_paths, std::string_view user_linker_flags)</arglist>
    </member>
    <member kind="function">
      <type>std::string</type>
      <name>truss::detail::triple_dir</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>gaedaa53fc693dff9d5becdbf41e6bf2b0</anchor>
      <arglist>(const TargetTriple &amp;t)</arglist>
    </member>
    <member kind="function">
      <type>CompilerFamily</type>
      <name>truss::detail::family_from_compiler_name</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>gafcc172548392de88d41faf4427056979</anchor>
      <arglist>(std::string_view name)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; TargetTriple &gt;</type>
      <name>truss::detail::apply_config</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga5bc696bd0bc1a0aa6be87e7e2972aa5a</anchor>
      <arglist>(const Config &amp;conf, const TargetTriple &amp;base)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; std::string &gt;</type>
      <name>truss::detail::resolve_launcher</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>gaa41e0d980cd7fa5d226fd22e031a85d8</anchor>
      <arglist>(const Config &amp;conf)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>truss::detail::copy_tree</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga8f4fbbbca03cb6fb3896881b09976675</anchor>
      <arglist>(const std::filesystem::path &amp;from, const std::filesystem::path &amp;to)</arglist>
    </member>
    <member kind="function">
      <type>std::string</type>
      <name>truss::detail::esc_value</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>gaa41d5db9cd0f627411024f6b1b1b310d</anchor>
      <arglist>(std::string_view v)</arglist>
    </member>
    <member kind="function">
      <type>std::string</type>
      <name>truss::detail::esc_path</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga1aef11ad00aa13c8a7408fb01f69e087</anchor>
      <arglist>(std::string_view p)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>truss::detail::samu_build</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga183f6507b11b7ad74131e5396e19e9ed</anchor>
      <arglist>(std::string_view manifest, int jobs, bool verbose, bool dry_run, bool explain, std::span&lt; const std::string &gt; targets)</arglist>
    </member>
    <member kind="function">
      <type>Result&lt; void &gt;</type>
      <name>truss::detail::samu_compdb</name>
      <anchorfile>group__grp__internals.html</anchorfile>
      <anchor>ga4f664ae39cf7b593f94c7f1d25754f2a</anchor>
      <arglist>(std::string_view manifest, std::string_view outpath)</arglist>
    </member>
  </compound>
  <compound kind="page">
    <name>internals_page</name>
    <title>Internals</title>
    <filename>internals_page.html</filename>
    <docanchor file="internals_page.html" title="docs/internals.md">md_docs_2internals</docanchor>
  </compound>
  <compound kind="dir">
    <name>docs</name>
    <path>docs/</path>
    <filename>dir_49e56c817e5e54854c35e136979f97ca.html</filename>
  </compound>
  <compound kind="dir">
    <name>src</name>
    <path>src/</path>
    <filename>dir_68267d1309a1af8e8297ef4c3efbcdba.html</filename>
    <file>api.cppm</file>
    <file>impl.cppm</file>
  </compound>
</tagfile>
