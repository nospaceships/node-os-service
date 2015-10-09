{
  'targets': [
    {
      'target_name': 'service',
      "include_dirs" : [
        "<!(node -e \"require('nan')\")"
      ],
      'conditions' : [
        ['OS=="win"', {
          'libraries' : ['advapi32.lib'],
          'sources': [
            'src/service.cc',
            'src/pthread.cc'
          ],
        }]
      ]
    }
  ]
}
